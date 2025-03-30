// compose.cc
#include <stdlib.h>
#include <stdint.h>

// Простая реализация системы слоёв.
// Каждый слой имеет размеры, z-координату и буфер пикселей в формате ARGB.
#define MAX_LAYERS 10

typedef struct {
    int z;
    int width;
    int height;
    uint32_t *data; // массив пикселей (ARGB)
} CTLayer;

// Глобальный массив указателей на слои и их количество.
static CTLayer* layers[MAX_LAYERS];
static int num_layers = 0;

// Добавление нового слоя. Инициализируется цветом init_color.
CTLayer* CTAddLayer(int z, int width, int height, uint32_t init_color) {
    CTLayer* layer = (CTLayer*)malloc(sizeof(CTLayer));
    if (!layer)
        return NULL;
    layer->width = width;
    layer->height = height;
    layer->z = z;
    layer->data = (uint32_t*)malloc(width * height * sizeof(uint32_t));
    if (!layer->data) {
        free(layer);
        return NULL;
    }
    for (int i = 0; i < width * height; i++) {
         layer->data[i] = init_color;
    }
    if (num_layers < MAX_LAYERS) {
         layers[num_layers++] = layer;
         // Сортируем слои по z (от нижнего к верхнему)
         for (int i = 0; i < num_layers; i++) {
             for (int j = i + 1; j < num_layers; j++) {
                 if (layers[i]->z > layers[j]->z) {
                     CTLayer* tmp = layers[i];
                     layers[i] = layers[j];
                     layers[j] = tmp;
                 }
             }
         }
         return layer;
    } else {
         free(layer->data);
         free(layer);
         return NULL;
    }
}

// Возвращает указатель на данные слоя для редактирования.
uint32_t* CTEditLayer(CTLayer* layer) {
    return layer->data;
}

// Удаляет слой, освобождая память.
int CTRemoveLayer(CTLayer* layer) {
    int found = 0;
    for (int i = 0; i < num_layers; i++) {
         if (layers[i] == layer) {
             found = 1;
             free(layer->data);
             free(layer);
             for (int j = i; j < num_layers - 1; j++) {
                 layers[j] = layers[j + 1];
             }
             num_layers--;
             break;
         }
    }
    return found;
}

// Композиция всех слоев в один итоговый буфер.
// Слои компонуются в порядке возрастания z.
// Итоговый буфер dest должен иметь размеры width*height.
void CTComposeLayers(uint32_t* dest, int width, int height) {
    int num_pixels = width * height;
    // Изначально очищаем итоговый буфер прозрачным (альфа = 0)
    for (int i = 0; i < num_pixels; i++) {
         dest[i] = 0x00000000;
    }
    // Накладываем каждый слой по порядку.
    // Применяем простое альфа-блендинг: out = src * alpha + dst * (1 - alpha)
    for (int l = 0; l < num_layers; l++) {
         CTLayer* layer = layers[l];
         for (int i = 0; i < num_pixels; i++) {
             uint32_t src = layer->data[i];
             uint8_t src_a = (src >> 24) & 0xFF;
             if (src_a == 0)
                continue;
             float alpha = src_a / 255.0f;
             uint32_t dst = dest[i];
             uint8_t dst_r = (dst >> 16) & 0xFF;
             uint8_t dst_g = (dst >> 8) & 0xFF;
             uint8_t dst_b = dst & 0xFF;
             uint8_t src_r = (src >> 16) & 0xFF;
             uint8_t src_g = (src >> 8) & 0xFF;
             uint8_t src_b = src & 0xFF;
             uint8_t out_r = (uint8_t)(src_r * alpha + dst_r * (1 - alpha));
             uint8_t out_g = (uint8_t)(src_g * alpha + dst_g * (1 - alpha));
             uint8_t out_b = (uint8_t)(src_b * alpha + dst_b * (1 - alpha));
             dest[i] = (0xFF << 24) | (out_r << 16) | (out_g << 8) | out_b;
         }
    }
}
