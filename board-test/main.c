/*
 * RP2350-PiZero board test.
 *
 * - cycles the WS2812 status LED (GPIO2) red / green / blue
 * - prints a status line once per second on USB CDC (USB-C "power" port)
 *   and on UART0 (GPIO0 TX / GPIO1 RX, 115200 8N1)
 * - reports chip info, SD card-detect state and whether PSRAM answers
 */
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/sync.h"
#include "pico/unique_id.h"
#include "ws2812.pio.h"

static PIO led_pio = pio0;
static uint led_sm;

static void led_put(uint8_t r, uint8_t g, uint8_t b)
{
    /* WS2812 expects GRB, MSB first, left-aligned in the 32-bit word */
    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
    pio_sm_put_blocking(led_pio, led_sm, grb << 8u);
}

/*
 * Read the PSRAM KGD/EID via the QMI direct-mode interface on CS1.
 * Returns the two ID bytes (MFID << 8 | KGD) or 0 if nothing answers.
 * Only reads the ID; the XIP window for CS1 is left unconfigured.
 */
static uint16_t __no_inline_not_in_flash_func(psram_read_id)(void)
{
    uint8_t mfid = 0, kgd = 0;
    uint32_t irq = save_and_disable_interrupts();
    int i;

    qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS)
        ;

    /* exit QPI mode in case the chip is in it (0xF5 in quad) */
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    qmi_hw->direct_tx = QMI_DIRECT_TX_OE_BITS
        | QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB | 0xf5;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS)
        ;
    (void)qmi_hw->direct_rx;
    qmi_hw->direct_csr &= ~QMI_DIRECT_CSR_ASSERT_CS1N_BITS;

    /* 0x9F read ID, 24-bit dummy address, then MFID, KGD */
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    for (i = 0; i < 7; i++) {
        qmi_hw->direct_tx = (i == 0) ? 0x9f : 0xff;
        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_TXEMPTY_BITS) == 0)
            ;
        while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS)
            ;
        if (i == 5)
            mfid = qmi_hw->direct_rx;
        else if (i == 6)
            kgd = qmi_hw->direct_rx;
        else
            (void)qmi_hw->direct_rx;
    }
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS | QMI_DIRECT_CSR_EN_BITS);

    restore_interrupts(irq);
    return (uint16_t)(mfid << 8 | kgd);
}

int main(void)
{
    static const uint8_t colours[3][3] = { {32, 0, 0}, {0, 32, 0}, {0, 0, 32} };
    pico_unique_board_id_t id;
    uint16_t psram_id;
    uint offset;
    uint32_t n = 0;
    int i;

    stdio_init_all();

    offset = pio_add_program(led_pio, &ws2812_program);
    led_sm = pio_claim_unused_sm(led_pio, true);
    ws2812_program_init(led_pio, led_sm, offset, PICO_DEFAULT_WS2812_PIN, 800000);

    gpio_init(PIZERO_SD_CD_PIN);
    gpio_set_dir(PIZERO_SD_CD_PIN, GPIO_IN);
    gpio_pull_up(PIZERO_SD_CD_PIN);

    /* route GPIO47 to QMI CS1 so the PSRAM (if fitted) can be probed */
    gpio_set_function(PIZERO_PSRAM_CS_PIN, GPIO_FUNC_XIP_CS1);
    psram_id = psram_read_id();

    pico_get_unique_board_id(&id);

    for (;;) {
        led_put(colours[n % 3][0], colours[n % 3][1], colours[n % 3][2]);

        printf("\n=== RP2350-PiZero board test, tick %lu ===\n", (unsigned long)n);
        printf("clk_sys   : %lu Hz\n", (unsigned long)clock_get_hz(clk_sys));
        printf("board id  : ");
        for (i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
            printf("%02x", id.id[i]);
        printf("\n");
        printf("SD detect : GPIO%d = %d\n", PIZERO_SD_CD_PIN, gpio_get(PIZERO_SD_CD_PIN));
        printf("PSRAM id  : MFID=0x%02x KGD=0x%02x %s\n", psram_id >> 8, psram_id & 0xff,
               ((psram_id >> 8) != 0x00 && (psram_id >> 8) != 0xff) ? "(chip answers)" : "(no PSRAM detected)");

        n++;
        sleep_ms(1000);
    }
}
