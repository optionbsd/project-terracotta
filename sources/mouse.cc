// cursor.cc
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <sys/mouse.h>
#include <sys/consio.h>

// Объявляем структуру и функцию загрузки изображения из image.cc.
struct TCImage {
    uint32_t* pixels;
    int width;
    int height;
};

TCImage TCImageToArray(const char* path);

// Глобальные переменные для курсора
static int sysmouse_fd = -1;
static int cursor_x = 0;
static int cursor_y = 0;
static int screen_width = 0;
static int screen_height = 0;
static TCImage global_cursor; // Изображение курсора (загружается один раз)

// Инициализация курсора: устанавливаются размеры экрана, загружается изображение и открывается /dev/sysmouse.
extern "C" void CTInitCursor(int scr_w, int scr_h) {
    screen_width = scr_w;
    screen_height = scr_h;
    global_cursor = TCImageToArray("build/res/cursor.png");
    // Начальное положение курсора по центру экрана (курсор полностью виден)
    cursor_x = (screen_width - global_cursor.width) / 2;
    cursor_y = (screen_height - global_cursor.height) / 2;
    sysmouse_fd = open("/dev/sysmouse", O_RDONLY | O_NONBLOCK);
    if (sysmouse_fd < 0) {
        perror("CTInitCursor: Ошибка открытия /dev/sysmouse");
    }
}

// Обновление позиции курсора с использованием 5-байтных пакетов sysmouse.
// Вертикальное смещение инвертируется, чтобы движение вниз двигало курсор вниз.
extern "C" void CTUpdateCursor() {
    if (sysmouse_fd < 0)
        return;
    unsigned char packet[5];
    ssize_t n;
    while ((n = read(sysmouse_fd, packet, 5)) == 5) {
        int dx = ((int)((signed char)packet[1])) + ((int)((signed char)packet[3]));
        int dy = -(((int)((signed char)packet[2])) + ((int)((signed char)packet[4])));
        cursor_x += dx;
        cursor_y += dy;
        // Ограничение курсора: слева и сверху не выходит за пределы (минимум 0),
        // справа и снизу курсор может выйти за пределы экрана на размер своей ширины/высоты.
        if (cursor_x < 0)
            cursor_x = 0;
        if (cursor_y < 0)
            cursor_y = 0;
        if (cursor_x > screen_width)
            cursor_x = screen_width;
        if (cursor_y > screen_height)
            cursor_y = screen_height;
    }
}

// Рисует курсор на слое. Здесь оптимизировано: вместо цикла по всему экрану очищается весь слой через memset.
extern "C" void CTDrawCursorOnLayer(uint32_t* layer_data, int layer_width, int layer_height) {
    int num_pixels = layer_width * layer_height;
    // Очищаем слой прозрачным цветом
    memset(layer_data, 0, num_pixels * sizeof(uint32_t));
    // Рисуем курсор
    for (int y = 0; y < global_cursor.height; y++) {
        int dst_y = cursor_y + y;
        if (dst_y < 0 || dst_y >= layer_height)
            continue;
        for (int x = 0; x < global_cursor.width; x++) {
            int dst_x = cursor_x + x;
            if (dst_x < 0 || dst_x >= layer_width)
                continue;
            uint32_t src_pixel = global_cursor.pixels[y * global_cursor.width + x];
            uint8_t src_a = (src_pixel >> 24) & 0xFF;
            if (src_a == 0)
                continue;
            layer_data[dst_y * layer_width + dst_x] = src_pixel;
        }
    }
}
