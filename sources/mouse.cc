// mouse.cc
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

// Для работы с sysmouse используем системные заголовки
#include <sys/mouse.h>
#include <sys/consio.h>

// Объявляем структуру и функцию загрузки изображения из image.cc.
struct TCImage {
    uint32_t* pixels;
    int width;
    int height;
};

TCImage TCImageToArray(const char* path);

// --- Глобальные переменные для работы с мышью и курсором ---
static int sysmouse_fd = -1;
static int cursor_x = 0;
static int cursor_y = 0;
static int screen_width = 0;
static int screen_height = 0;
static TCImage global_cursor; // Изображение курсора загружается один раз

// Функция инициализации мыши и загрузки курсора.
// Загружается курсор, вычисляется его размер, позиция устанавливается по центру экрана, 
// открывается /dev/sysmouse (ожидается, что moused запущен).
void InitMouse(int scr_w, int scr_h) {
    screen_width = scr_w;
    screen_height = scr_h;
    
    // Загружаем изображение курсора один раз
    global_cursor = TCImageToArray("build/res/cursor.png");

    // Устанавливаем начальное положение курсора по центру экрана
    cursor_x = (screen_width - global_cursor.width) / 2;
    cursor_y = (screen_height - global_cursor.height) / 2;

    sysmouse_fd = open("/dev/sysmouse", O_RDONLY | O_NONBLOCK);
    if (sysmouse_fd < 0) {
        perror("Ошибка открытия /dev/sysmouse");
        // Если открыть устройство не удалось, продолжим без обновления позиции
    }
}

// Обновление положения курсора.
// Согласно документации для уровня 0, sysmouse возвращает 5-байтные пакеты.
// Формат пакета:
//   Byte1: фиксирован (бит7 всегда установлен)
//   Byte2: первая половина горизонтального смещения (в знаковом виде)
//   Byte3: первая половина вертикального смещения (в знаковом виде)
//   Byte4: вторая половина горизонтального смещения (в знаковом виде)
//   Byte5: вторая половина вертикального смещения (в знаковом виде)
// Итоговое смещение по X = (signed)Byte2 + (signed)Byte4, по Y = (signed)Byte3 + (signed)Byte5.
void UpdateCursorPosition() {
    if (sysmouse_fd < 0)
        return;
    unsigned char packet[5];
    ssize_t n;
    // Читаем все доступные 5-байтные пакеты
    while ((n = read(sysmouse_fd, packet, 5)) == 5) {
        int dx = ((int)((signed char)packet[1])) + ((int)((signed char)packet[3]));
        int dy = ((int)((signed char)packet[2])) + ((int)((signed char)packet[4]));
        cursor_x += dx;
        cursor_y += dy;
        // Ограничиваем положение курсора в пределах экрана
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_y < 0) cursor_y = 0;
        if (cursor_x > screen_width - global_cursor.width)
            cursor_x = screen_width - global_cursor.width;
        if (cursor_y > screen_height - global_cursor.height)
            cursor_y = screen_height - global_cursor.height;
    }
    // Если read возвращает -1 и errno == EAGAIN, значит, данных больше нет – это нормально.
}

// Функция отрисовки курсора по заданным координатам (верхний левый угол).
// Использует ранее загруженное изображение курсора global_cursor.
extern "C" void DrawCursorAt(void* fb_buffer, int fb_width, int fb_height, int pos_x, int pos_y) {
    uint32_t* fb = (uint32_t*)fb_buffer;
    for (int y = 0; y < global_cursor.height; y++) {
        int fb_y = pos_y + y;
        if (fb_y < 0 || fb_y >= fb_height)
            continue;
        for (int x = 0; x < global_cursor.width; x++) {
            int fb_x = pos_x + x;
            if (fb_x < 0 || fb_x >= fb_width)
                continue;
            
            uint32_t src_pixel = global_cursor.pixels[y * global_cursor.width + x];
            uint8_t src_a = (src_pixel >> 24) & 0xFF;
            uint8_t src_r = (src_pixel >> 16) & 0xFF;
            uint8_t src_g = (src_pixel >> 8) & 0xFF;
            uint8_t src_b = src_pixel & 0xFF;

            uint32_t dst_pixel = fb[fb_y * fb_width + fb_x];
            uint8_t dst_r = (dst_pixel >> 16) & 0xFF;
            uint8_t dst_g = (dst_pixel >> 8) & 0xFF;
            uint8_t dst_b = dst_pixel & 0xFF;

            float alpha = src_a / 255.0f;
            uint8_t out_r = (uint8_t)(src_r * alpha + dst_r * (1.0f - alpha));
            uint8_t out_g = (uint8_t)(src_g * alpha + dst_g * (1.0f - alpha));
            uint8_t out_b = (uint8_t)(src_b * alpha + dst_b * (1.0f - alpha));
            uint32_t out_pixel = (0xFF << 24) | (out_r << 16) | (out_g << 8) | out_b;
            fb[fb_y * fb_width + fb_x] = out_pixel;
        }
    }
}

// Главный цикл обработки событий мыши: очищает экран (заливая его чёрным), обновляет положение курсора
// и отрисовывает его. Цикл работает с приблизительной частотой 60 FPS.
extern "C" void MouseEventLoop(void* fb_buffer, int fb_width, int fb_height) {
    // Инициализируем мышь и загружаем курсор
    InitMouse(fb_width, fb_height);
    uint32_t* fb = (uint32_t*)fb_buffer;
    size_t num_pixels = fb_width * fb_height;

    while (1) {
        // Заливаем фон чёрным (ARGB: 0xFF000000)
        for (size_t i = 0; i < num_pixels; i++) {
            fb[i] = 0xFF000000;
        }
        // Обновляем положение курсора, читая данные sysmouse
        UpdateCursorPosition();
        // Отрисовываем курсор в текущей позиции
        DrawCursorAt(fb_buffer, fb_width, fb_height, cursor_x, cursor_y);
        // Задержка ~16 мс для ~60 FPS
        usleep(16000);
    }
}
