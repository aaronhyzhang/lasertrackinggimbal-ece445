/* H753 Nucleo Ethernet-only bring-up. No motor/sensor initialization. */
#include "main.h"
/* USER CODE BEGIN Includes */
#include "network.h"
#include "ethernetif.h"
#include <string.h>
/* USER CODE END Includes */

ETH_HandleTypeDef heth;
ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT]
    __attribute__((section(".RxDescripSection"), aligned(32)));
ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT]
    __attribute__((section(".TxDescripSection"), aligned(32)));

/* USER CODE BEGIN PV */
static uint8_t mac_address[6];
/* USER CODE END PV */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ETH_Init(void);

int main(void)
{
    /* USER CODE BEGIN 1 */
    SCB_EnableICache();
    /* D-cache stays disabled: descriptor and packet DMA needs coherent RAM. */
    /* USER CODE END 1 */
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    /* USER CODE BEGIN SysInit */
    ethernetif_prepare();
    memset(DMARxDscrTab, 0, sizeof(DMARxDscrTab));
    memset(DMATxDscrTab, 0, sizeof(DMATxDscrTab));
    /* USER CODE END SysInit */
    MX_ETH_Init();
    /* USER CODE BEGIN 2 */
    if (!network_init()) Error_Handler();
    uint32_t last_blink = HAL_GetTick();
    /* USER CODE END 2 */
    while (1) {
        /* USER CODE BEGIN 3 */
        network_poll();
        uint32_t now = HAL_GetTick();
        if (now - last_blink >= 500u) {
            last_blink = now;
            HAL_GPIO_TogglePin(GPIOE, GPIO_PIN_1); /* Yellow LD2: alive. */
        }
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0,
            network_link_up() ? GPIO_PIN_SET : GPIO_PIN_RESET); /* Green LD1: link. */
        /* USER CODE END 3 */
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clock = {0};
    if (HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK) Error_Handler();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    uint32_t start = HAL_GetTick();
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {
        if (HAL_GetTick() - start > 100u) Error_Handler();
    }
    /* CPU clock is independent of ST-LINK MCO/HSE. PHY supplies RMII 50 MHz. */
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    oscillator.HSIState = RCC_HSI_DIV1;
    oscillator.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    oscillator.PLL.PLLM = 16;
    oscillator.PLL.PLLN = 100;
    oscillator.PLL.PLLP = 2;
    oscillator.PLL.PLLQ = 4;
    oscillator.PLL.PLLR = 2;
    oscillator.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
    oscillator.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) Error_Handler();
    clock.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
        RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_D3PCLK1;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock.SYSCLKDivider = RCC_SYSCLK_DIV1;
    clock.AHBCLKDivider = RCC_HCLK_DIV2;
    clock.APB1CLKDivider = RCC_APB1_DIV2;
    clock.APB2CLKDivider = RCC_APB2_DIV2;
    clock.APB3CLKDivider = RCC_APB3_DIV2;
    clock.APB4CLKDivider = RCC_APB4_DIV2;
    if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_4) != HAL_OK) Error_Handler();
}

static void MX_GPIO_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIO_InitTypeDef pin = {0};
    /* Preserve disabled PG4 on the previous bench harness. No timers are started. */
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_4, GPIO_PIN_RESET);
    pin.Pin = GPIO_PIN_4;
    pin.Mode = GPIO_MODE_OUTPUT_PP;
    pin.Pull = GPIO_PULLDOWN;
    pin.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOG, &pin);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_14, GPIO_PIN_RESET);
    pin.Pin = GPIO_PIN_0 | GPIO_PIN_14;
    pin.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &pin);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_1, GPIO_PIN_RESET);
    pin.Pin = GPIO_PIN_1;
    HAL_GPIO_Init(GPIOE, &pin);
}

static void MX_ETH_Init(void)
{
    /* USER CODE BEGIN ETH_Init 1 */
    uint32_t id = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();
    mac_address[0] = 0x02; /* Locally administered unicast address. */
    mac_address[1] = 0x47;
    mac_address[2] = (uint8_t)(id >> 24);
    mac_address[3] = (uint8_t)(id >> 16);
    mac_address[4] = (uint8_t)(id >> 8);
    mac_address[5] = (uint8_t)id;
    /* USER CODE END ETH_Init 1 */
    heth.Instance = ETH;
    heth.Init.MACAddr = mac_address;
    heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
    heth.Init.RxDesc = DMARxDscrTab;
    heth.Init.TxDesc = DMATxDscrTab;
    heth.Init.RxBuffLen = 1536;
    if (HAL_ETH_Init(&heth) != HAL_OK) Error_Handler();
}

void Error_Handler(void)
{
    __disable_irq();
    if (__HAL_RCC_GPIOG_IS_CLK_ENABLED())
        GPIOG->BSRR = (uint32_t)GPIO_PIN_4 << 16;
    if (__HAL_RCC_GPIOB_IS_CLK_ENABLED())
        GPIOB->BSRR = GPIO_PIN_14; /* Red LD3: fatal init/assert/CPU fault. */
    for (;;) {}
}
