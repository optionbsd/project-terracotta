// image.cc
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Структура для хранения изображения: массив пикселей, ширина и высота.
struct TCImage {
    uint32_t* pixels;
    int width;
    int height;
};

// Функция загружает PNG, конвертирует его в ARGB8888 и возвращает данные.
TCImage TCImageToArray(const char *path) {
    int w, h, channels;
    unsigned char* data = stbi_load(path, &w, &h, &channels, 4);
    if (!data) {
         fprintf(stderr, "Не удалось загрузить изображение %s\n", path);
         exit(EXIT_FAILURE);
    }
    size_t num_pixels = w * h;
    uint32_t *pixels = (uint32_t*)malloc(num_pixels * sizeof(uint32_t));
    if (!pixels) {
         fprintf(stderr, "Ошибка выделения памяти для пикселей\n");
         stbi_image_free(data);
         exit(EXIT_FAILURE);
    }
    for (size_t i = 0; i < num_pixels; i++) {
         unsigned char r = data[i * 4 + 0];
         unsigned char g = data[i * 4 + 1];
         unsigned char b = data[i * 4 + 2];
         unsigned char a = data[i * 4 + 3];
         uint32_t pixel = (a << 24) | (r << 16) | (g << 8) | b;
         pixels[i] = pixel;
    }
    stbi_image_free(data);
    TCImage img;
    img.pixels = pixels;
    img.width = w;
    img.height = h;
    return img;
}
