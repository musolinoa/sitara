#define DRAM_BASE 0x80000000
#define DRAM_TOP 0xC0000000
#define UART_BASE 0x44E09000
#define INTCPS_BASE 0x48200000

#define TIMER0_BASE 0x44e05000
#define TIMER1_BASE 0x44e31000
#define TIMER2_BASE 0x48040000
#define TIMER3_BASE 0x48042000
#define TIMER4_BASE 0x48044000
#define TIMER5_BASE 0x48046000
#define TIMER6_BASE 0x48048000
#define TIMER7_BASE 0x4804a000

#define GPIO0_BASE	0x44e07000
#define GPIO1_BASE	0x4804c000
#define GPIO2_BASE	0x481ac000
#define GPIO3_BASE	0x481ae000

#define ETH_BASE	0x4a100000

#define CPRM_BASE	0x44e00000

#define SRAM_BASE 0x40300000
#define SRAM_SIZE 64*1024

#define ETHIRQRXTHR		40
#define ETHIRQRX		41
#define ETHIRQTX		42
#define ETHIRQMISC		43

#define TIMER0IRQ 66
#define TIMER1IRQ 67
#define TIMER2IRQ 68
#define TIMER3IRQ 69

#define UART0IRQ 72
