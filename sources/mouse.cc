// mouse.cc
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Объявляем структуру и функцию из image.cc,
// чтобы использовать их здесь.
struct TCImage {
    uint32_t* pixels;
    int width;
    int height;
};

TCImage TCImageToArray(const char* path);

extern "C" void DrawCursor(void* fb_buffer, int fb_width, int fb_height) {
    // Загружаем курсор в виде массива пикселей
    TCImage cursor = TCImageToArray("/home/hp/Terracotta/build/res/cursor.png");

    // Вычисляем отступы для центрирования изображения
    int x_offset = (fb_width - cursor.width) / 2;
    int y_offset = (fb_height - cursor.height) / 2;

    uint32_t* fb = (uint32_t*)fb_buffer;

    // Проходим по каждому пикселю изображения курсора
    for (int y = 0; y < cursor.height; y++) {
        if (y + y_offset < 0 || y + y_offset >= fb_height)
            continue;
        for (int x = 0; x < cursor.width; x++) {
            if (x + x_offset < 0 || x + x_offset >= fb_width)
                continue;

            // Исходный пиксель из курсора
            uint32_t src_pixel = cursor.pixels[y * cursor.width + x];
            uint8_t src_a = (src_pixel >> 24) & 0xFF;
            uint8_t src_r = (src_pixel >> 16) & 0xFF;
            uint8_t src_g = (src_pixel >> 8) & 0xFF;
            uint8_t src_b = src_pixel & 0xFF;

            // Пиксель назначения (из framebuffer)
            uint32_t dst_pixel = fb[(y + y_offset) * fb_width + (x + x_offset)];
            uint8_t dst_r = (dst_pixel >> 16) & 0xFF;
            uint8_t dst_g = (dst_pixel >> 8) & 0xFF;
            uint8_t dst_b = dst_pixel & 0xFF;

            // Применяем альфа-блендинг:
            // out = src * alpha + dst * (1 - alpha)
            float alpha = src_a / 255.0f;
            uint8_t out_r = (uint8_t)(src_r * alpha + dst_r * (1.0f - alpha));
            uint8_t out_g = (uint8_t)(src_g * alpha + dst_g * (1.0f - alpha));
            uint8_t out_b = (uint8_t)(src_b * alpha + dst_b * (1.0f - alpha));
            // Результирующий пиксель делаем полностью непрозрачным
            uint32_t out_pixel = (0xFF << 24) | (out_r << 16) | (out_g << 8) | out_b;
            fb[(y + y_offset) * fb_width + (x + x_offset)] = out_pixel;
        }
    }
    free(cursor.pixels);
}
