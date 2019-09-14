/*-
 * Copyright (c) 2018-2019 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/console.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/thread.h>

#include <arm/arm/nvic.h>
#include <arm/arm/scb.h>
#include <arm/nordicsemi/nrf9160.h>

#include <machine/cpuregs.h>

#include "errata.h"

struct uarte_softc uarte_sc;
struct arm_nvic_softc nvic_sc;
struct spu_softc spu_sc;
struct power_softc power_sc;
struct arm_scb_softc scb_sc;

#define	UART_PIN_TX	29
#define	UART_PIN_RX	21
#define	UART_BAUDRATE	115200

#define	APP_ENTRY	0x40000

void app_main(void);
void jump_ns(uint32_t addr);

static void
uart_putchar(int c, void *arg)
{
	struct uarte_softc *sc;
 
	sc = arg;
 
	if (c == '\n')
		uarte_putc(sc, '\r');

	uarte_putc(sc, c);
}

static void
secure_boot_configure_periph(int periph_id)
{

	spu_periph_set_attr(&spu_sc, periph_id, 0, 0);
	arm_nvic_disable_intr(&nvic_sc, periph_id);
	arm_nvic_target_ns(&nvic_sc, periph_id, 0);
}

static void
secure_boot_configure(void)
{
	int i;

	for (i = 0; i < 8; i++)
		spu_flash_set_perm(&spu_sc, i, 1);
	for (i = 8; i < 32; i++)
		spu_flash_set_perm(&spu_sc, i, 0);

	for (i = 0; i < 8; i++)
		spu_sram_set_perm(&spu_sc, i, 1);
	for (i = 8; i < 32; i++)
		spu_sram_set_perm(&spu_sc, i, 0);

	spu_gpio_set_perm(&spu_sc, 0, 0);

	secure_boot_configure_periph(ID_CLOCK);
	secure_boot_configure_periph(ID_RTC1);
	secure_boot_configure_periph(ID_IPC);
	secure_boot_configure_periph(ID_NVMC);
	secure_boot_configure_periph(ID_VMC);
	secure_boot_configure_periph(ID_GPIO);
	secure_boot_configure_periph(ID_GPIOTE1);
	secure_boot_configure_periph(ID_UARTE1);
	secure_boot_configure_periph(ID_EGU1);
	secure_boot_configure_periph(ID_EGU2);
	secure_boot_configure_periph(ID_FPU);
	secure_boot_configure_periph(ID_TWIM2);
	secure_boot_configure_periph(ID_SPIM3);
	secure_boot_configure_periph(ID_TIMER0);
}

int
app_init(void)
{

	uarte_init(&uarte_sc, BASE_UARTE0 | PERIPH_SECURE_ACCESS,
	    UART_PIN_TX, UART_PIN_RX, UART_BAUDRATE);
	console_register(uart_putchar, (void *)&uarte_sc);

	return (0);
}

int
main(void)
{
	uint32_t control_ns;
	uint32_t msp_ns;
	uint32_t psp_ns;
	uint32_t *vec;

	printf("Hello world!\n");

	power_init(&power_sc, BASE_POWER | PERIPH_SECURE_ACCESS);
	errata_init();

	spu_init(&spu_sc, BASE_SPU);
	arm_nvic_init(&nvic_sc, BASE_SCS);

	secure_boot_configure();

	arm_scb_init(&scb_sc, BASE_SCS_NS);
	arm_scb_set_vector(&scb_sc, APP_ENTRY);
	arm_scb_init(&scb_sc, BASE_SCS);

	vec = (uint32_t *)APP_ENTRY;

	msp_ns = vec[0];
	psp_ns = 0;

	__asm __volatile("msr msp_ns, %0" :: "r" (msp_ns));
	__asm __volatile("msr psp_ns, %0" :: "r" (psp_ns));

	__asm __volatile("mrs %0, control_ns" : "=r" (control_ns));
	control_ns &= ~CONTROL_SPSEL; /* Use main stack pointer. */
	control_ns &= ~CONTROL_NPRIV; /* Set privilege mode. */
	__asm __volatile("msr control_ns, %0" :: "r" (control_ns));

	arm_scb_exceptions_prio_config(&scb_sc, 1);
	arm_scb_exceptions_target_config(&scb_sc, 0);
	arm_scb_sysreset_secure(&scb_sc, 0);
	arm_sau_configure(&scb_sc, 0, 1);
	arm_fpu_non_secure(&scb_sc, 1);

	printf("Jumping to non-secure address 0x%x\n", vec[1]);

	secure_boot_configure_periph(ID_UARTE0);

	jump_ns(vec[1]);

	/* UNREACHABLE */

	return (0);
}