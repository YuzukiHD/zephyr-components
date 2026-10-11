/*
 * Calls the functions of <zephyr/drivers/uart.h> on UART1, the console of the YuzukiNeko (PB00/PB01), so
 * that the other end of the cable, the host, can answer, and on UART4 (PD6/PD7).
 */
#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/ring_buffer.h>

static const struct device *const uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static const struct device *const uart4 = DEVICE_DT_GET(DT_NODELABEL(uart4));

static const char *err(int ret)
{
	switch (ret) {
	case 0:		return "0";
	case -1:	return "-1";
	case -EINVAL:	return "-EINVAL";
	case -ENOTSUP:	return "-ENOTSUP";
	case -ENOSYS:	return "-ENOSYS";
	default:	return "?";
	}
}

/* uart_config_get: the current baud rate, parity, stop bits, data bits and flow control.
 * The enum values: parity 0 none, 1 odd, 2 even; stop bits 0 half, 1 one, 2 one and a half, 3 two;
 * data bits 0..3 are 5..8; flow control 0 none, 1 RTS/CTS, 2 DTR/DSR.
 */
static void demo_config_get(void)
{
	struct uart_config now;
	int ret = uart_config_get(uart, &now);

	printk("[config] uart_config_get = %s: %u baud, parity %d, stop bits %d, data bits %d, flow control %d\n",
	       err(ret), now.baudrate, now.parity, now.stop_bits, now.data_bits, now.flow_ctrl);
}

/* uart_configure: apply a new setting, and read it back with uart_config_get */
static void demo_configure(void)
{
	static const struct uart_config table[] = {
		{ .baudrate = 9600, .parity = UART_CFG_PARITY_NONE, .stop_bits = UART_CFG_STOP_BITS_1,
		  .data_bits = UART_CFG_DATA_BITS_8, .flow_ctrl = UART_CFG_FLOW_CTRL_NONE },
		{ .baudrate = 921600, .parity = UART_CFG_PARITY_NONE, .stop_bits = UART_CFG_STOP_BITS_1,
		  .data_bits = UART_CFG_DATA_BITS_8, .flow_ctrl = UART_CFG_FLOW_CTRL_NONE },
		{ .baudrate = 115200, .parity = UART_CFG_PARITY_EVEN, .stop_bits = UART_CFG_STOP_BITS_1,
		  .data_bits = UART_CFG_DATA_BITS_8, .flow_ctrl = UART_CFG_FLOW_CTRL_NONE },
		{ .baudrate = 115200, .parity = UART_CFG_PARITY_ODD, .stop_bits = UART_CFG_STOP_BITS_1,
		  .data_bits = UART_CFG_DATA_BITS_8, .flow_ctrl = UART_CFG_FLOW_CTRL_NONE },
		{ .baudrate = 115200, .parity = UART_CFG_PARITY_NONE, .stop_bits = UART_CFG_STOP_BITS_2,
		  .data_bits = UART_CFG_DATA_BITS_8, .flow_ctrl = UART_CFG_FLOW_CTRL_NONE },
		{ .baudrate = 115200, .parity = UART_CFG_PARITY_NONE, .stop_bits = UART_CFG_STOP_BITS_1_5,
		  .data_bits = UART_CFG_DATA_BITS_8, .flow_ctrl = UART_CFG_FLOW_CTRL_NONE },
		{ .baudrate = 115200, .parity = UART_CFG_PARITY_NONE, .stop_bits = UART_CFG_STOP_BITS_1,
		  .data_bits = UART_CFG_DATA_BITS_7, .flow_ctrl = UART_CFG_FLOW_CTRL_NONE },
		{ .baudrate = 115200, .parity = UART_CFG_PARITY_NONE, .stop_bits = UART_CFG_STOP_BITS_1,
		  .data_bits = UART_CFG_DATA_BITS_8, .flow_ctrl = UART_CFG_FLOW_CTRL_RTS_CTS },
		{ .baudrate = 115200, .parity = UART_CFG_PARITY_NONE, .stop_bits = UART_CFG_STOP_BITS_1,
		  .data_bits = UART_CFG_DATA_BITS_8, .flow_ctrl = UART_CFG_FLOW_CTRL_DTR_DSR },
	};
	struct uart_config now, back[ARRAY_SIZE(table)];
	int ret[ARRAY_SIZE(table)];

	uart_config_get(uart, &now);

	/* The console is this very UART, so nothing is printed while a different setting is active,
	 * and the transmitter is given time to send what is still in it before the first change.
	 * Each setting is applied and read back, then the original is restored and the results printed.
	 */
	k_msleep(50);
	for (int i = 0; i < ARRAY_SIZE(table); i++) {
		ret[i] = uart_configure(uart, &table[i]);
		uart_config_get(uart, &back[i]);
	}
	uart_configure(uart, &now);

	for (int i = 0; i < ARRAY_SIZE(table); i++) {
		printk("[config] %6u baud, parity %d, stop bits %d, data bits %d, flow %d = %s; read back %u, %d, %d, %d, %d\n",
		       table[i].baudrate, table[i].parity, table[i].stop_bits, table[i].data_bits,
		       table[i].flow_ctrl, err(ret[i]), back[i].baudrate, back[i].parity,
		       back[i].stop_bits, back[i].data_bits, back[i].flow_ctrl);
	}
}

/* uart_poll_out: one character, waits until the transmitter has room */
static void demo_poll_out(void)
{
	static const char text[] = "[poll_out] Hello from uart_poll_out\r\n";

	for (int i = 0; text[i] != '\0'; i++) {
		uart_poll_out(uart, text[i]);
	}
}

/* uart_poll_in: returns 0 with the character, or -1 when nothing has arrived */
static void demo_poll_in(void)
{
	unsigned char c = 0;
	char got[16] = { 0 };
	int n = 0;
	int ret = uart_poll_in(uart, &c);

	printk("[poll_in] nothing sent yet: uart_poll_in = %s\n", err(ret));
	printk("[poll_in] waiting up to 20 s for 5 characters on the console\n");

	for (int64_t end = k_uptime_get() + 20000; n < 5 && k_uptime_get() < end;) {
		if (uart_poll_in(uart, &c) == 0) {
			got[n++] = c;
		} else {
			k_msleep(1);
		}
	}
	printk("[poll_in] got %d characters: \"%s\"\n", n, got);
}

/* interrupt driven receive and transmit */
RING_BUF_DECLARE(rx_ring, 64);
static const char *tx_text;
static int tx_left;
static volatile int rx_irqs, tx_irqs;

/* uart_fifo_read: take what the receiver has out of its FIFO */
static void rx_ready(const struct device *dev)
{
	uint8_t buf[16];
	int n = uart_fifo_read(dev, buf, sizeof(buf));

	ring_buf_put(&rx_ring, buf, n);
	rx_irqs++;
}

/* uart_fifo_fill: put as much as fits into the transmit FIFO; the rest waits for the next interrupt */
static void tx_ready(const struct device *dev)
{
	tx_irqs++;
	if (tx_left > 0) {
		int n = uart_fifo_fill(dev, (const uint8_t *)tx_text, tx_left);

		tx_text += n;
		tx_left -= n;
	}
	if (tx_left == 0) {
		uart_irq_tx_disable(dev);
	}
}

/* the interrupt handler: uart_irq_update latches the interrupt state, uart_irq_is_pending says
 * whether there is anything to do, uart_irq_rx_ready and uart_irq_tx_ready say what
 */
static void uart_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			rx_ready(dev);
		}
		if (uart_irq_tx_ready(dev)) {
			tx_ready(dev);
		}
	}
}

/* uart_irq_callback_user_data_set: the handler that the UART interrupt calls */
static void demo_irq_callback_set(void)
{
	int ret = uart_irq_callback_user_data_set(uart, uart_isr, NULL);

	printk("[irq] uart_irq_callback_user_data_set = %s\n", err(ret));
}

/* uart_irq_tx_enable: the handler fills the FIFO whenever it has room */
static void demo_irq_tx_enable(void)
{
	static const char msg[] = "[irq] sent by the interrupt handler, 64 bytes at the most per fill ........\r\n";

	tx_text = msg;
	tx_left = strlen(msg);
	uart_irq_tx_enable(uart);
	k_msleep(100);
	printk("[irq] transmit: %d interrupt(s), %d byte(s) left\n", tx_irqs, tx_left);
}

/* uart_irq_rx_enable: the host sends 4 characters; the handler puts them in a ring buffer.
 * uart_irq_rx_disable stops the receive interrupt again.
 */
static void demo_irq_rx_enable(void)
{
	uint8_t buf[32];

	uart_irq_rx_enable(uart);
	printk("[irq] waiting up to 20 s for 4 characters on the console\n");
	for (int64_t end = k_uptime_get() + 20000; ring_buf_size_get(&rx_ring) < 4 && k_uptime_get() < end;) {
		k_msleep(10);
	}
	k_msleep(20);
	int n = ring_buf_get(&rx_ring, buf, sizeof(buf) - 1);

	buf[n] = '\0';
	uart_irq_rx_disable(uart);
	printk("[irq] receive: %d interrupt(s), got %d characters: \"%s\"\n", rx_irqs, n, buf);
}

/* uart_err_check: a bit mask of the errors seen since the last call */
static void demo_errors(void)
{
	int ret = uart_err_check(uart);

	printk("[error] uart_err_check = 0x%x (0 = no overrun, parity, framing or break error)\n", ret);
}

/* UART4 on PD6 (TX) and PD7 (RX): nothing is connected there on the board */
static void demo_uart4(void)
{
	static const char text[] = "uart4\r\n";
	unsigned char c;
	struct uart_config cfg;
	int ret;

	printk("[uart4] device_is_ready = %d\n", device_is_ready(uart4));
	ret = uart_config_get(uart4, &cfg);
	printk("[uart4] uart_config_get = %s: %u baud\n", err(ret), cfg.baudrate);

	for (int i = 0; text[i] != '\0'; i++) {
		uart_poll_out(uart4, text[i]);
	}
	printk("[uart4] sent %d bytes on PD6\n", (int)strlen(text));

	/* PD7 is not connected: take whatever is in the receiver and ask for the line errors */
	int n = 0;

	c = 0;
	while (n < 64 && uart_poll_in(uart4, &c) == 0) {
		n++;
	}
	ret = uart_poll_in(uart4, &c);
	printk("[uart4] %d byte(s) were waiting in the receiver, then uart_poll_in = %s\n", n, err(ret));
	printk("[uart4] uart_err_check = 0x%x\n", uart_err_check(uart4));
}

int main(void)
{
	if (!device_is_ready(uart) || !device_is_ready(uart4)) {
		printk("uart not ready\n");
		return 0;
	}

	demo_config_get();
	demo_configure();
	demo_poll_out();
	demo_poll_in();
	demo_irq_callback_set();
	demo_irq_tx_enable();
	demo_irq_rx_enable();
	demo_errors();
	demo_uart4();

	printk("uart_api done\n");
	return 0;
}
