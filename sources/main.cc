// main.cc
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <stdint.h>
#include <errno.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

// Объявления функций композиции слоёв из compose.cc
typedef struct {
    int z;
    int width;
    int height;
    uint32_t *data;
} CTLayer;

extern CTLayer* CTAddLayer(int z, int width, int height, uint32_t init_color);
extern uint32_t* CTEditLayer(CTLayer* layer);
extern int CTRemoveLayer(CTLayer* layer);
extern void CTComposeLayers(uint32_t* dest, int width, int height);

// Объявления функций для работы с курсором из cursor.cc
extern "C" void CTInitCursor(int scr_w, int scr_h);
extern "C" void CTUpdateCursor();
extern "C" void CTDrawCursorOnLayer(uint32_t* layer_data, int layer_width, int layer_height);

int main() {
    // Открываем DRM-устройство (обычно /dev/dri/card0)
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("Ошибка открытия /dev/dri/card0");
        return EXIT_FAILURE;
    }
    
    // Получаем ресурсы устройства
    drmModeRes *resources = drmModeGetResources(fd);
    if (!resources) {
        perror("drmModeGetResources не удалось получить ресурсы");
        close(fd);
        return EXIT_FAILURE;
    }
    
    // Ищем подключённый коннектор
    drmModeConnector *connector = NULL;
    uint32_t connector_id = 0;
    for (int i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(fd, resources->connectors[i]);
        if (connector && connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0) {
            connector_id = connector->connector_id;
            break;
        }
        drmModeFreeConnector(connector);
        connector = NULL;
    }
    if (!connector) {
        fprintf(stderr, "Не найден подходящий коннектор\n");
        drmModeFreeResources(resources);
        close(fd);
        return EXIT_FAILURE;
    }
    
    // Выбираем первый доступный режим
    drmModeModeInfo mode = connector->modes[0];
    
    // Получаем энкодер для данного коннектора
    drmModeEncoder *encoder = drmModeGetEncoder(fd, connector->encoder_id);
    if (!encoder) {
        fprintf(stderr, "Не найден энкодер\n");
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return EXIT_FAILURE;
    }
    uint32_t crtc_id = encoder->crtc_id;
    drmModeFreeEncoder(encoder);
    
    // Создаем "dumb buffer"
    struct drm_mode_create_dumb create = {};
    create.width = mode.hdisplay;
    create.height = mode.vdisplay;
    create.bpp = 32; // 32 бита на пиксель
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
        perror("DRM_IOCTL_MODE_CREATE_DUMB не выполнен");
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return EXIT_FAILURE;
    }
    uint32_t handle = create.handle;
    uint32_t pitch = create.pitch;
    uint64_t size = create.size;
    
    // Создаем фреймбуфер
    uint32_t fb;
    if (drmModeAddFB(fd, mode.hdisplay, mode.vdisplay, 24, 32, pitch, handle, &fb)) {
        perror("drmModeAddFB не выполнен");
        struct drm_mode_destroy_dumb destroy = {};
        destroy.handle = handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return EXIT_FAILURE;
    }
    
    // Маппим буфер в адресное пространство
    struct drm_mode_map_dumb map = {};
    map.handle = handle;
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map)) {
        perror("DRM_IOCTL_MODE_MAP_DUMB не выполнен");
        drmModeRmFB(fd, fb);
        struct drm_mode_destroy_dumb destroy = {};
        destroy.handle = handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return EXIT_FAILURE;
    }
    void *buffer = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
    if (buffer == MAP_FAILED) {
        perror("mmap не выполнен");
        drmModeRmFB(fd, fb);
        struct drm_mode_destroy_dumb destroy = {};
        destroy.handle = handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return EXIT_FAILURE;
    }
    
    // Устанавливаем режим с нашим фреймбуфером
    if (drmModeSetCrtc(fd, crtc_id, fb, 0, 0, &connector_id, 1, &mode)) {
        perror("drmModeSetCrtc не выполнен");
        munmap(buffer, size);
        drmModeRmFB(fd, fb);
        struct drm_mode_destroy_dumb destroy = {};
        destroy.handle = handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return EXIT_FAILURE;
    }
    
    // Создаем виртуальные слои:
    // Фон (z = 0): заливаем черным (непрозрачным)
    CTLayer* background = CTAddLayer(0, mode.hdisplay, mode.vdisplay, 0xFF000000);
    // Слой курсора (z = 1): изначально полностью прозрачный
    CTLayer* cursorLayer = CTAddLayer(1, mode.hdisplay, mode.vdisplay, 0x00000000);
    
    // Инициализируем курсор (загрузка изображения, открытие /dev/sysmouse)
    CTInitCursor(mode.hdisplay, mode.vdisplay);
    
    // Выделяем временный буфер для композиции всех слоев (итоговое изображение)
    uint32_t* composed = (uint32_t*)malloc(mode.hdisplay * mode.vdisplay * sizeof(uint32_t));
    if (!composed) {
         perror("Ошибка выделения памяти для композиции");
         // Освобождаем ресурсы...
         exit(EXIT_FAILURE);
    }
    
    // Главный цикл: 60 FPS – 60 раз в секунду обновляем слои, композицию и выводим итог на экран.
    while (1) {
         // Обновляем фон: заливаем background черным (непрозрачным)
         int num_pixels = mode.hdisplay * mode.vdisplay;
         for (int i = 0; i < num_pixels; i++) {
             background->data[i] = 0xFF000000;
         }
         
         // Обновляем позицию курсора
         CTUpdateCursor();
         // Обновляем слой курсора: очищаем его и рисуем курсор в текущей позиции
         CTDrawCursorOnLayer(cursorLayer->data, mode.hdisplay, mode.vdisplay);
         
         // Компонуем все слои (background и cursorLayer) в итоговый буфер composed
         CTComposeLayers(composed, mode.hdisplay, mode.vdisplay);
         // Копируем итоговое изображение в framebuffer
         memcpy(buffer, composed, num_pixels * sizeof(uint32_t));
         
         // Задержка ~16 мс для 60 FPS
         usleep(16000);
    }
    
    // Освобождение ресурсов (код недостижим, но оставлен для корректного завершения)
    free(composed);
    munmap(buffer, size);
    drmModeRmFB(fd, fb);
    {
         struct drm_mode_destroy_dumb destroy = {};
         destroy.handle = handle;
         drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    }
    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);
    close(fd);
    return EXIT_SUCCESS;
}
