/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "stm32f1xx_hal_gpio.h"
#include "tim.h"
#include "usart.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define LED_TOGGLE_MS 500 //定义一个宏：LED小灯的半周期

#define KEY_DEBOUNCE_CNT 20  //按键消抖检测次数
#define KEY_NUM 3  //数组长度/按键循环上界

#define TX_RING_SIZE 256U  //必须为 2 的幂，才能用按位与代替取余；256B ≈ 11 帧状态帧（23B/帧），50Hz 下约 0.2s 缓冲余量
#define TX_RING_MASK (TX_RING_SIZE - 1U)  //物理下标掩码：wr & MASK 等价于 wr % 256。若把容量改成非 2 的幂（如 200），& 199 不再等于 % 200，数据错位而编译零警告
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/*全局毫秒时基，TIM2更新中断每 1ms 自增一次*/
volatile uint32_t g_tick_ms = 0;  //volatile防止编译器把值缓存进寄存器导致主循环死等； uint32_t回绕周期 2^32ms 优于其它数据类型

volatile uint32_t g_key_press[KEY_NUM] = {0};  //按下次数绝对值
volatile uint32_t g_key_held[KEY_NUM] = {0};  //按键按下状态，长按也是有操作的

static uint8_t s_cnt[KEY_NUM] = {0};  //消抖计数
static uint8_t s_stable[KEY_NUM] = {1, 1, 1};  //消抖后的电平
static uint8_t s_event[KEY_NUM] = {0};  //按下沿标志
static uint32_t s_last_scan_tick = 0;  //1ms采样闸门，防止s_cnt暴涨卡死


static GPIO_TypeDef *const s_port[KEY_NUM] = {KEY_MODE_GPIO_Port, KEY_RESET_GPIO_Port, KEY_SPARE_GPIO_Port};
static const uint16_t s_pin[KEY_NUM] = {KEY_MODE_Pin, KEY_RESET_Pin, KEY_SPARE_Pin};

static uint8_t s_tx_ring[TX_RING_SIZE];  //环形缓冲器本体：printf 出来的字节先落这里，等 USB 空闲再由 cdc_pump 搬走
static uint16_t s_tx_wr;  //写索引=下一个要写的位置。只增不减、永不手动归零；靠 uint16 自然溢出回绕，差值 wr-rd 在模 65536 下恒等于真实已用字节数
static uint16_t s_tx_rd;  //读索引=下一个要读的位置。同样只增不减，且只能由「发送成功」那一步推进；任何一处把它赋 0 都会让 used 算错
volatile uint32_t g_tx_drop;  //因缓冲满而丢弃的字节数。>0 说明 printf 速度超过 USB 消费速度，是容量不足的唯一硬证据；代码不读它，加 volatile 防止被优化掉
volatile uint32_t g_tx_sent;  //已交给 USB 外设的字节数，只代表「已入端点」不代表主机收到；与 g_tx_drop 一起核对总量守恒
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void key_scan(void);  //消抖函数的声明，定义在BEGIN 4
static uint16_t ring_push(const uint8_t *p, uint16_t len);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  uint32_t last_toggle = 0;  //初始化一个上次翻转的时间点

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_USART1_UART_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */
  //启动计数器
  if (HAL_TIM_Base_Start_IT(&htim2) != HAL_OK) {
    Error_Handler();
  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    //心跳指示灯
    uint32_t now = g_tick_ms;

    if ((now - last_toggle) >= LED_TOGGLE_MS){
      last_toggle += LED_TOGGLE_MS;
      HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    }

    //按键消抖逻辑的接入
    if (now != s_last_scan_tick) {
      s_last_scan_tick = now;
      key_scan();
    }

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
//回调函数定义时基机制
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
  if (htim->Instance == TIM2){   //确认定时器
    g_tick_ms++;  //每1ms计数加一
  }
}

//按键消抖：每 1ms 调用一次（由主循环的 tick 闸门保证），连续 KEY_DEBOUNCE_CNT 次
//电平一致才认可状态变化，避免机械触点抖动被识别成多次按下
static void key_scan(void) {
  for (int i = 0; i < KEY_NUM; i++) {
    //按下=0（引脚经按键接地），松开=1（内部上拉）
    uint8_t raw = (HAL_GPIO_ReadPin(s_port[i], s_pin[i]) == GPIO_PIN_RESET) ? 0 : 1;

    if (raw == s_stable[i]) {
      s_cnt[i] = 0;                        //与稳定态一致：计数清零，不做动作
    } else {
      s_cnt[i]++;                          //与稳定态不一致：进入观察期，开始累加

      if (s_cnt[i] >= KEY_DEBOUNCE_CNT) {  //连续 20 次(20ms)都不一致 → 确认真的翻转
        s_cnt[i] = 0;
        s_stable[i] = raw;                 //更新稳定态

        if (raw == 0) {                    //翻转后的新状态是「按下」→ 才记一次事件
          s_event[i] = 1;                  //一次性标志，留给 T6 状态机读后清零
          g_key_press[i]++;                //累计按下次数，供 ST-Link 只读验证
        }
      }
    }
    g_key_held[i] = (s_stable[i] == 0) ? 1 : 0;  //当前是否按住，供长按/组合键判断
  }
}

//环形缓冲器的定义
static uint16_t ring_push(const uint8_t *p, uint16_t len) {
  uint16_t used = (uint16_t)(s_tx_wr - s_tx_rd);  //强转防止整型提升导致回绕偏差， 不转会的到负数

  uint16_t spare = TX_RING_SIZE - used;

  //收敛 spare（缓冲剩余空间）
  if (len > spare) {
    g_tx_drop += (len - spare);
    len = spare;
  }
  //逐字节搬运
  for (int i = 0; i < len; i++) {
    s_tx_ring[s_tx_wr & TX_RING_MASK] = p[i];
    s_tx_wr++;
  }

  return len;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
