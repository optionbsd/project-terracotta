// compose.cc
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#define MAX_LAYERS 10

typedef struct {
    int z;           // Глубина слоя: меньшие значения — ниже, большие — выше
    int width;       // Ширина слоя
    int height;      // Высота слоя
    uint32_t *data;  // Массив пикселей в формате ARGB (8 бит на канал)
} CTLayer;

static CTLayer* layers[MAX_LAYERS];
static int num_layers = 0;

// Функция добавления нового слоя с заданным начальным цветом.
CTLayer* CTAddLayer(int z, int width, int height, uint32_t init_color) {
    CTLayer* layer = (CTLayer*)malloc(sizeof(CTLayer));
    if (!layer)
        return NULL;
    layer->z = z;
    layer->width = width;
    layer->height = height;
    size_t num_pixels = (size_t)width * height;
    layer->data = (uint32_t*)malloc(num_pixels * sizeof(uint32_t));
    if (!layer->data) {
        free(layer);
        return NULL;
    }
    // Если init_color == 0, используем memset для быстрого заполнения
    if (init_color == 0) {
        memset(layer->data, 0, num_pixels * sizeof(uint32_t));
    } else {
        for (size_t i = 0; i < num_pixels; i++) {
            layer->data[i] = init_color;
        }
    }
    if (num_layers < MAX_LAYERS) {
        layers[num_layers++] = layer;
        // Сортировка слоёв по z (от нижнего к верхнему)
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

// Функция возвращает указатель на данные слоя для редактирования.
uint32_t* CTEditLayer(CTLayer* layer) {
    return layer->data;
}

// Функция удаления слоя. Возвращает ненулевое значение, если удаление прошло успешно.
int CTRemoveLayer(CTLayer* layer) {
    int found = 0;
    for (int i = 0; i < num_layers; i++) {
        if (layers[i] == layer) {
            found = 1;
            free(layer->data);
            free(layer);
            // Сдвигаем оставшиеся слои, чтобы не было "дыр"
            for (int j = i; j < num_layers - 1; j++) {
                layers[j] = layers[j + 1];
            }
            num_layers--;
            break;
        }
    }
    return found;
}

// Функция компоновки слоёв.
// dest - указатель на итоговый буфер, width и height - размеры изображения.
// Первый слой (индекс 0) считается фоном, последующие накладываются сверху.
void CTComposeLayers(uint32_t* dest, int width, int height) {
    int num_pixels = width * height;
    if (num_layers < 1)
        return;

    // Копируем фон напрямую.
    memcpy(dest, layers[0]->data, num_pixels * sizeof(uint32_t));

    // Обрабатываем каждый дополнительный слой.
    for (int l = 1; l < num_layers; l++) {
        CTLayer* top = layers[l];
        uint32_t *src = top->data;

        // Параллелизация по пикселям с использованием OpenMP
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < num_pixels; i += 4) {
            // Разворачиваем цикл вручную по 4 пикселя.
            for (int k = 0; k < 4 && (i + k) < num_pixels; k++) {
                uint32_t src_pixel = src[i + k];
                uint8_t src_a = src_pixel >> 24;  // Извлекаем альфа-канал
                if (src_a == 0)
                    continue;
                else if (src_a == 255) {  // Полностью непрозрачный пиксель
                    dest[i + k] = src_pixel;
                } else {
                    // Извлекаем компоненты фонового пикселя
                    uint32_t dst_pixel = dest[i + k];
                    uint8_t dst_r = (dst_pixel >> 16) & 0xFF;
                    uint8_t dst_g = (dst_pixel >> 8) & 0xFF;
                    uint8_t dst_b = dst_pixel & 0xFF;
                    // Извлекаем компоненты пикселя источника
                    uint8_t src_r = (src_pixel >> 16) & 0xFF;
                    uint8_t src_g = (src_pixel >> 8) & 0xFF;
                    uint8_t src_b = src_pixel & 0xFF;
                    // Выполняем альфа‑блендинг:
                    // out = (src * alpha + dst * (255 - alpha)) / 255
                    uint8_t out_r = (uint8_t)((src_r * src_a + dst_r * (255 - src_a)) / 255);
                    uint8_t out_g = (uint8_t)((src_g * src_a + dst_g * (255 - src_a)) / 255);
                    uint8_t out_b = (uint8_t)((src_b * src_a + dst_b * (255 - src_a)) / 255);
                    dest[i + k] = (255u << 24) | (out_r << 16) | (out_g << 8) | out_b;
                }
            }
        }
    }
}
