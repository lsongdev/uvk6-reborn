@ PY32F071xB start-up. The bootloader occupies 0x08000000-0x080027FF and has
@ already set the 48 MHz clock; the application's vector table follows it.

	.syntax unified
	.cpu cortex-m0plus
	.fpu softvfp
	.thumb

	.global HandlerReset
	.extern _estack
	.extern DATA_Init
	.extern BSS_Init
	.extern Main

	.section .isr_vector, "a", %progbits
	.type g_pfnVectors, %object
g_pfnVectors:
	.word	_estack
	.word	HandlerReset
	.word	HandlerNMI
	.word	HandlerHardFault
	.word	0
	.word	0
	.word	0
	.word	0
	.word	0
	.word	0
	.word	0
	.word	HandlerSVCall
	.word	0
	.word	0
	.word	HandlerPendSV
	.word	SystickHandler
	.word	WWDG_IRQHandler
	.word	PVD_IRQHandler
	.word	RTC_IRQHandler
	.word	FLASH_IRQHandler
	.word	RCC_IRQHandler
	.word	EXTI0_1_IRQHandler
	.word	EXTI2_3_IRQHandler
	.word	EXTI4_15_IRQHandler
	.word	LCD_IRQHandler
	.word	DMA1_Channel1_IRQHandler
	.word	DMA1_Channel2_3_IRQHandler
	.word	DMA1_Channel4_5_6_7_IRQHandler
	.word	ADC_COMP_IRQHandler
	.word	TIM1_BRK_UP_TRG_COM_IRQHandler
	.word	TIM1_CC_IRQHandler
	.word	TIM2_IRQHandler
	.word	TIM3_IRQHandler
	.word	TIM6_LPTIM1_DAC_IRQHandler
	.word	TIM7_IRQHandler
	.word	TIM14_IRQHandler
	.word	TIM15_IRQHandler
	.word	TIM16_IRQHandler
	.word	TIM17_IRQHandler
	.word	I2C1_IRQHandler
	.word	I2C2_IRQHandler
	.word	SPI1_IRQHandler
	.word	SPI2_IRQHandler
	.word	USART1_IRQHandler
	.word	USART2_IRQHandler
	.word	USART3_IRQHandler
	.word	0
	.word	USB_IRQHandler
	.size g_pfnVectors, .-g_pfnVectors

	.section .text.HandlerReset
	.type HandlerReset, %function
HandlerReset:
	cpsid	i
	@ The bootloader leaves VTOR on its own table: point it at ours first.
	ldr	r0, =0xE000ED08
	ldr	r1, =g_pfnVectors
	str	r1, [r0]
	@ Its interrupts too: disable and clear every external one before cpsie.
	ldr	r0, =0xE000E180            @ NVIC_ICER0
	ldr	r1, =0xFFFFFFFF
	str	r1, [r0]
	ldr	r0, =0xE000E280            @ NVIC_ICPR0
	str	r1, [r0]
	ldr	r0, =_estack
	mov	sp, r0
	bl	DATA_Init
	bl	BSS_Init
	cpsie	i
	bl	Main
_Reset_Loop:
	wfi
	b	_Reset_Loop
	.size HandlerReset, .-HandlerReset

	.section .text.Default_Handler, "ax", %progbits
Default_Handler:
	b	Default_Handler
	.size Default_Handler, .-Default_Handler

	.weak	HandlerNMI
	.thumb_set HandlerNMI, Default_Handler
	.weak	HandlerHardFault
	.thumb_set HandlerHardFault, Default_Handler
	.weak	HandlerSVCall
	.thumb_set HandlerSVCall, Default_Handler
	.weak	HandlerPendSV
	.thumb_set HandlerPendSV, Default_Handler
	.weak	SystickHandler
	.thumb_set SystickHandler, Default_Handler
	.weak	WWDG_IRQHandler
	.thumb_set WWDG_IRQHandler, Default_Handler
	.weak	PVD_IRQHandler
	.thumb_set PVD_IRQHandler, Default_Handler
	.weak	RTC_IRQHandler
	.thumb_set RTC_IRQHandler, Default_Handler
	.weak	FLASH_IRQHandler
	.thumb_set FLASH_IRQHandler, Default_Handler
	.weak	RCC_IRQHandler
	.thumb_set RCC_IRQHandler, Default_Handler
	.weak	EXTI0_1_IRQHandler
	.thumb_set EXTI0_1_IRQHandler, Default_Handler
	.weak	EXTI2_3_IRQHandler
	.thumb_set EXTI2_3_IRQHandler, Default_Handler
	.weak	EXTI4_15_IRQHandler
	.thumb_set EXTI4_15_IRQHandler, Default_Handler
	.weak	LCD_IRQHandler
	.thumb_set LCD_IRQHandler, Default_Handler
	.weak	DMA1_Channel1_IRQHandler
	.thumb_set DMA1_Channel1_IRQHandler, Default_Handler
	.weak	DMA1_Channel2_3_IRQHandler
	.thumb_set DMA1_Channel2_3_IRQHandler, Default_Handler
	.weak	DMA1_Channel4_5_6_7_IRQHandler
	.thumb_set DMA1_Channel4_5_6_7_IRQHandler, Default_Handler
	.weak	ADC_COMP_IRQHandler
	.thumb_set ADC_COMP_IRQHandler, Default_Handler
	.weak	TIM1_BRK_UP_TRG_COM_IRQHandler
	.thumb_set TIM1_BRK_UP_TRG_COM_IRQHandler, Default_Handler
	.weak	TIM1_CC_IRQHandler
	.thumb_set TIM1_CC_IRQHandler, Default_Handler
	.weak	TIM2_IRQHandler
	.thumb_set TIM2_IRQHandler, Default_Handler
	.weak	TIM3_IRQHandler
	.thumb_set TIM3_IRQHandler, Default_Handler
	.weak	TIM6_LPTIM1_DAC_IRQHandler
	.thumb_set TIM6_LPTIM1_DAC_IRQHandler, Default_Handler
	.weak	TIM7_IRQHandler
	.thumb_set TIM7_IRQHandler, Default_Handler
	.weak	TIM14_IRQHandler
	.thumb_set TIM14_IRQHandler, Default_Handler
	.weak	TIM15_IRQHandler
	.thumb_set TIM15_IRQHandler, Default_Handler
	.weak	TIM16_IRQHandler
	.thumb_set TIM16_IRQHandler, Default_Handler
	.weak	TIM17_IRQHandler
	.thumb_set TIM17_IRQHandler, Default_Handler
	.weak	I2C1_IRQHandler
	.thumb_set I2C1_IRQHandler, Default_Handler
	.weak	I2C2_IRQHandler
	.thumb_set I2C2_IRQHandler, Default_Handler
	.weak	SPI1_IRQHandler
	.thumb_set SPI1_IRQHandler, Default_Handler
	.weak	SPI2_IRQHandler
	.thumb_set SPI2_IRQHandler, Default_Handler
	.weak	USART1_IRQHandler
	.thumb_set USART1_IRQHandler, Default_Handler
	.weak	USART2_IRQHandler
	.thumb_set USART2_IRQHandler, Default_Handler
	.weak	USART3_IRQHandler
	.thumb_set USART3_IRQHandler, Default_Handler
	.weak	USB_IRQHandler
	.thumb_set USB_IRQHandler, Default_Handler
