#include "drivers/py32f071/port.h"
#include "hal/gpio.h"

GPIO_TypeDef *PY32_Port(uint8_t port) {
  static GPIO_TypeDef *const PORTS[] = {GPIOA, GPIOB, GPIOC, GPIOF};
  return PORTS[port];
}

void PY32_PinMode(uint8_t port, uint8_t pin, Py32PinMode mode, Py32Pull pull) {
  GPIO_TypeDef *gpio = PY32_Port(port);
  uint32_t shift = 2u * pin;
  gpio->OSPEEDR |= 3u << shift;
  gpio->OTYPER &= ~(1u << pin);
  gpio->PUPDR = (gpio->PUPDR & ~(3u << shift)) | ((uint32_t)pull << shift);
  gpio->MODER = (gpio->MODER & ~(3u << shift)) | ((uint32_t)mode << shift);
}

void PY32_PinAlternate(uint8_t port, uint8_t pin, uint8_t af, Py32Pull pull) {
  GPIO_TypeDef *gpio = PY32_Port(port);
  uint32_t shift = 4u * (pin & 7u);
  gpio->AFR[pin >> 3] = (gpio->AFR[pin >> 3] & ~(15u << shift)) | ((uint32_t)af << shift);
  PY32_PinMode(port, pin, PY32_ALTERNATE, pull);
}

void GPIO_Write(GpioLine line, bool high) {
  const Py32GpioLine *l = &GPIO_LINES[line];
  if (l->port == PY32_NO_LINE) return;
  GPIO_TypeDef *gpio = PY32_Port(l->port);
  if (high)
    gpio->BSRR = 1u << l->pin;
  else
    gpio->BRR = 1u << l->pin;
}

bool GPIO_Read(GpioLine line) {
  const Py32GpioLine *l = &GPIO_LINES[line];
  return l->port != PY32_NO_LINE && (PY32_Port(l->port)->IDR >> l->pin & 1u);
}

void GPIO_SetInput(GpioLine line, bool input) {
  const Py32GpioLine *l = &GPIO_LINES[line];
  if (l->port == PY32_NO_LINE) return;
  GPIO_TypeDef *gpio = PY32_Port(l->port);
  uint32_t shift = 2u * l->pin;
  gpio->MODER = (gpio->MODER & ~(3u << shift)) |
                ((uint32_t)(input ? PY32_INPUT : PY32_OUTPUT) << shift);
}
