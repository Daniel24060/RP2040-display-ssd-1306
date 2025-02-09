// main.c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"

#include "inc/ssd1306.h"   // Driver do display SSD1306 (baseado em render_area)
#include "hardware/i2c.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2818b.pio.h"    // Programa PIO para WS2812

// =====================
// DEFINIÇÕES DOS PINOS
// =====================


#define BUTTON_A_PIN    6
#define BUTTON_B_PIN    5

// LED RGB (usado para feedback visual)
// Conforme solicitado: Botão A controla o LED verde e Botão B controla o LED azul.
// (O LED vermelho pode ser usado para outras finalidades, se necessário.)
#define LED_RED_PIN     13
#define LED_GREEN_PIN   11
#define LED_BLUE_PIN    12

// Matriz de LEDs endereçáveis WS2812 (5x5 = 25 LEDs)
#define WS2812_PIN      7
#define LED_COUNT       25

// Display SSD1306 – uso de i2c1 (SDA = 14, SCL = 15)
const uint I2C_SDA = 14;
const uint I2C_SCL = 15;

// =====================
// VARIÁVEIS GLOBAIS
// =====================

// Flags para os botões (setadas nas interrupções com debounce)
volatile bool button_a_pressed = false;
volatile bool button_b_pressed = false;

// Estados dos LEDs RGB (para alternar)
bool led_green_state = false;
bool led_blue_state  = false;

// =====================
// WS2812 – MATRIZ 5x5
// =====================

typedef struct {
    uint8_t G, R, B;   // Ordem GRB (usado pelos WS2812)
} npLED_t;

npLED_t leds[LED_COUNT];
PIO np_pio;
uint sm;  // Máquina de estados do PIO

// Mapa para exibir números (0 a 9) na matriz 5×5; cada linha do array representa uma "linha" de bits.
const uint8_t numbers[10][5] = {
    {0b11111, 0b10001, 0b10001, 0b10001, 0b11111}, // 0
    {0b00100, 0b01100, 0b00100, 0b00100, 0b01110}, // 1
    {0b11111, 0b00001, 0b11111, 0b10000, 0b11111}, // 2
    {0b11111, 0b00001, 0b01110, 0b00001, 0b11111}, // 3
    {0b10001, 0b10001, 0b11111, 0b00001, 0b00001}, // 4
    {0b11111, 0b10000, 0b11111, 0b00001, 0b11111}, // 5
    {0b11111, 0b10000, 0b11111, 0b10001, 0b11111}, // 6
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000}, // 7
    {0b11111, 0b10001, 0b11111, 0b10001, 0b11111}, // 8
    {0b11111, 0b10001, 0b11111, 0b00001, 0b11111}  // 9
};

// Função para mapear as coordenadas (x,y) da matriz 5x5 para o índice do array (serpenteado)
int getIndex_ws2812(int x, int y) {
    return (y % 2 == 0) ? (y * 5 + x) : ((y + 1) * 5 - 1 - x);
}

// Inicializa os WS2812 via PIO
void npInit(uint pin) {
    uint offset = pio_add_program(pio0, &ws2818b_program);
    np_pio = pio0;
    sm = pio_claim_unused_sm(np_pio, true);
    ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);
    memset(leds, 0, sizeof(leds));
}

// Define a cor de um LED individual da matriz
void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b) {
    leds[index].R = r;
    leds[index].G = g;
    leds[index].B = b;
}

// Desliga todos os LEDs da matriz
void npClear() {
    memset(leds, 0, sizeof(leds));
}

// Envia os dados para os WS2812
void npWrite() {
    for (uint i = 0; i < LED_COUNT; ++i) {
        pio_sm_put_blocking(np_pio, sm, leds[i].G);
        pio_sm_put_blocking(np_pio, sm, leds[i].R);
        pio_sm_put_blocking(np_pio, sm, leds[i].B);
    }
    sleep_us(100);
}

// Exibe um número (0–9) na matriz WS2812 usando a cor vermelha
void displayNumber(int num) {
    npClear();
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            if (numbers[num][y] & (1 << (4 - x))) {
                npSetLED(getIndex_ws2812(x, y), 255, 0, 0);
            }
        }
    }
    npWrite();
}

// =====================
// FONTES 5x7 PARA EXIBIÇÃO NO DISPLAY SSD1306
// =====================

// Fonte para letras maiúsculas (A-Z) – cada caractere ocupa 5 bytes
static const uint8_t font5x7_upper[26 * 5] = {
    // A
    0x7C, 0x12, 0x11, 0x12, 0x7C,
    // B
    0x7F, 0x49, 0x49, 0x49, 0x36,
    // C
    0x3E, 0x41, 0x41, 0x41, 0x22,
    // D
    0x7F, 0x41, 0x41, 0x22, 0x1C,
    // E
    0x7F, 0x49, 0x49, 0x49, 0x41,
    // F
    0x7F, 0x09, 0x09, 0x09, 0x01,
    // G
    0x3E, 0x41, 0x49, 0x49, 0x7A,
    // H
    0x7F, 0x08, 0x08, 0x08, 0x7F,
    // I
    0x00, 0x41, 0x7F, 0x41, 0x00,
    // J
    0x20, 0x40, 0x41, 0x3F, 0x01,
    // K
    0x7F, 0x08, 0x14, 0x22, 0x41,
    // L
    0x7F, 0x40, 0x40, 0x40, 0x40,
    // M
    0x7F, 0x02, 0x0C, 0x02, 0x7F,
    // N
    0x7F, 0x04, 0x08, 0x10, 0x7F,
    // O
    0x3E, 0x41, 0x41, 0x41, 0x3E,
    // P
    0x7F, 0x09, 0x09, 0x09, 0x06,
    // Q
    0x3E, 0x41, 0x51, 0x21, 0x5E,
    // R
    0x7F, 0x09, 0x19, 0x29, 0x46,
    // S
    0x46, 0x49, 0x49, 0x49, 0x31,
    // T
    0x01, 0x01, 0x7F, 0x01, 0x01,
    // U
    0x3F, 0x40, 0x40, 0x40, 0x3F,
    // V
    0x1F, 0x20, 0x40, 0x20, 0x1F,
    // W
    0x3F, 0x40, 0x38, 0x40, 0x3F,
    // X
    0x63, 0x14, 0x08, 0x14, 0x63,
    // Y
    0x07, 0x08, 0x70, 0x08, 0x07,
    // Z
    0x61, 0x51, 0x49, 0x45, 0x43
};

// Fonte para letras minúsculas (a-z) – cada caractere ocupa 5 bytes  
static const uint8_t font5x7_lower[26 * 5] = {
    // a
    0x20, 0x54, 0x54, 0x54, 0x78,
    // b
    0x7F, 0x48, 0x44, 0x44, 0x38,
    // c
    0x38, 0x44, 0x44, 0x44, 0x20,
    // d
    0x38, 0x44, 0x44, 0x48, 0x7F,
    // e
    0x38, 0x54, 0x54, 0x54, 0x18,
    // f
    0x08, 0x7E, 0x09, 0x01, 0x02,
    // g
    0x0C, 0x52, 0x52, 0x52, 0x3E,
    // h
    0x7F, 0x08, 0x04, 0x04, 0x78,
    // i
    0x00, 0x44, 0x7D, 0x40, 0x00,
    // j
    0x20, 0x40, 0x44, 0x3D, 0x00,
    // k
    0x7F, 0x10, 0x28, 0x44, 0x00,
    // l
    0x00, 0x41, 0x7F, 0x40, 0x00,
    // m
    0x7C, 0x04, 0x18, 0x04, 0x78,
    // n
    0x7C, 0x08, 0x04, 0x04, 0x78,
    // o
    0x38, 0x44, 0x44, 0x44, 0x38,
    // p
    0x7C, 0x14, 0x14, 0x14, 0x08,
    // q
    0x08, 0x14, 0x14, 0x18, 0x7C,
    // r
    0x7C, 0x08, 0x04, 0x04, 0x08,
    // s
    0x48, 0x54, 0x54, 0x54, 0x20,
    // t
    0x04, 0x3F, 0x44, 0x40, 0x20,
    // u
    0x3C, 0x40, 0x40, 0x20, 0x7C,
    // v
    0x1C, 0x20, 0x40, 0x20, 0x1C,
    // w
    0x3C, 0x40, 0x30, 0x40, 0x3C,
    // x
    0x44, 0x28, 0x10, 0x28, 0x44,
    // y
    0x0C, 0x50, 0x50, 0x50, 0x3C,
    // z
    0x44, 0x64, 0x54, 0x4C, 0x44
};

// ---------------------
// FUNÇÕES CUSTOMIZADAS PARA EXIBIÇÃO NO DISPLAY SSD1306
// ---------------------
// Desenha um caractere (5x7) na posição (x,y) usando a fonte apropriada
void ssd1306_draw_char_custom(uint8_t *buffer, int x, int y, char c) {
    const uint8_t *font;
    int index;
    
    if(c >= 'A' && c <= 'Z') {
        font = font5x7_upper;
        index = c - 'A';
    } else if(c >= 'a' && c <= 'z') {
        font = font5x7_lower;
        index = c - 'a';
    } else if(c == ' ') {
        // Espaço: pula 5 pixels (não desenha)
        return;
    } else {
        // Caracter não suportado: pula
        return;
    }
    
    // Cada caractere tem 5 colunas; desenha 7 linhas
    for (int col = 0; col < 5; col++) {
        uint8_t line = font[index * 5 + col];
        for (int row = 0; row < 7; row++) {
            bool pixel_on = (line >> row) & 0x01;
            ssd1306_set_pixel(buffer, x + col, y + row, pixel_on);
        }
    }
}

// Desenha uma string usando a função customizada; pula 1 pixel entre caracteres.
void ssd1306_draw_string_custom(uint8_t *buffer, int x, int y, const char *str) {
    int orig_x = x;
    while(*str) {
        if(*str == '\n') {
            y += 8;  // Avança para a próxima linha
            x = orig_x;
        } else {
            ssd1306_draw_char_custom(buffer, x, y, *str);
            x += 6;  // 5 pixels de caractere + 1 pixel de espaçamento
        }
        str++;
    }
}

// ---------------------
// DISPLAY SSD1306 (UTILIZANDO O MÉTODO RENDER_AREA)
// ---------------------

// Buffer para o display (128x64: 128*64/8 = 1024 bytes)
static uint8_t ssd[1024];

// Área de renderização (normalmente, a área completa do display)
struct render_area frame_area;

// Inicializa o display SSD1306
void init_display() {
    i2c_init(i2c1, ssd1306_i2c_clock * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    
    ssd1306_init();
    
    frame_area.start_column = 0;
    frame_area.end_column   = ssd1306_width - 1;
    frame_area.start_page   = 0;
    frame_area.end_page     = ssd1306_n_pages - 1;
    calculate_render_area_buffer_length(&frame_area);
    
    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);
}
  
// Exibe uma mensagem no display SSD1306 usando as funções customizadas (suporta minúsculas e maiúsculas)
void displayOnSSD1306(const char *message) {
    memset(ssd, 0, ssd1306_buffer_length);
    ssd1306_draw_string_custom(ssd, 5, 0, message);
    render_on_display(ssd, &frame_area);
}
  
// ---------------------
// INTERRUPÇÃO E DEBOUNCE PARA OS BOTÕES
// ---------------------

void button_a_callback(uint gpio, uint32_t events) {
    uint32_t now = time_us_32();
    static uint32_t last_time_a = 0;
    if (now - last_time_a < 50000) return;  // 50 ms debounce
    last_time_a = now;
    button_a_pressed = true;
}

void button_b_callback(uint gpio, uint32_t events) {
    uint32_t now = time_us_32();
    static uint32_t last_time_b = 0;
    if (now - last_time_b < 50000) return;
    last_time_b = now;
    button_b_pressed = true;
}

// ---------------------
// INICIALIZAÇÃO DO HARDWARE
// ---------------------

void init_hardware() {
    stdio_init_all();
    
    // Inicializa a UART (usa uart0 com baudrate 115200)
    uart_init(uart0, 115200);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);
    
    // Configuração dos botões com pull-up e interrupção (debounce)
    gpio_init(BUTTON_A_PIN);
    gpio_set_dir(BUTTON_A_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_A_PIN);
    gpio_set_irq_enabled_with_callback(BUTTON_A_PIN, GPIO_IRQ_EDGE_RISE, true, &button_a_callback);
    
    gpio_init(BUTTON_B_PIN);
    gpio_set_dir(BUTTON_B_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_B_PIN);
    gpio_set_irq_enabled_with_callback(BUTTON_B_PIN, GPIO_IRQ_EDGE_RISE, true, &button_b_callback);
    
    // Configuração dos LEDs RGB (saídas)
    gpio_init(LED_RED_PIN);
    gpio_set_dir(LED_RED_PIN, GPIO_OUT);
    gpio_init(LED_GREEN_PIN);
    gpio_set_dir(LED_GREEN_PIN, GPIO_OUT);
    gpio_init(LED_BLUE_PIN);
    gpio_set_dir(LED_BLUE_PIN, GPIO_OUT);
    
    // Inicializa o display SSD1306 (i2c1)
    init_display();
    
    // Inicializa a matriz de LEDs WS2812
    npInit(WS2812_PIN);
}
  
// ---------------------
// FUNÇÃO PRINCIPAL
// ---------------------

int main() {
    init_hardware();
    
    char input[20];
    
    while (true) {
        // Leitura via UART (Serial Monitor)
        printf("Digite um caractere ou string: ");
        if (fgets(input, sizeof(input), stdin) != NULL) {
            // Remove as quebras de linha
            input[strcspn(input, "\r\n")] = '\0';
            if (strlen(input) > 0) {
                // Exibe a mensagem digitada no display OLED (suporta minúsculas)
                displayOnSSD1306(input);
                
                // Se o primeiro caractere for um dígito, exibe o símbolo correspondente na matriz WS2812
                if (input[0] >= '0' && input[0] <= '9') {
                    int num = input[0] - '0';
                    displayNumber(num);
                }
            }
        }
        
        // ===== Funcionalidade 3: Interação com o Botão A =====
        // Ao pressionar o botão A, alterna o LED verde (GPIO 11) e registra a operação:
        // - Exibe uma mensagem informativa no display
        // - Envia um texto descritivo ao Serial Monitor
        if (button_a_pressed) {
            button_a_pressed = false;
            led_green_state = !led_green_state;
            gpio_put(LED_GREEN_PIN, led_green_state);
            
            char msg[30];
            sprintf(msg, "LED Verde %s", led_green_state ? "ligado" : "desligado");
            printf("%s\n", msg);         // Envia para o Serial Monitor
            displayOnSSD1306(msg);         // Exibe no display SSD1306
        }
        
        // ===== Funcionalidade 4: Interação com o Botão B =====
        // Ao pressionar o botão B, alterna o LED azul (GPIO 12) e registra a operação:
        // - Exibe uma mensagem informativa no display
        // - Envia um texto descritivo ao Serial Monitor
        if (button_b_pressed) {
            button_b_pressed = false;
            led_blue_state = !led_blue_state;
            gpio_put(LED_BLUE_PIN, led_blue_state);
            
            char msg[30];
            sprintf(msg, "LED Azul %s", led_blue_state ? "ligado" : "desligado");
            printf("%s\n", msg);         // Envia para o Serial Monitor
            displayOnSSD1306(msg);         // Exibe no display SSD1306
        }
        
        sleep_ms(100);
    }
    
    return 0;
}
