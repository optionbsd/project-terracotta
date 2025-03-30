// main.cc
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <stdint.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

// Объявления функций компоновки из compose.cc
typedef struct {
    int z;
    int width;
    int height;
    uint32_t *data;
} CTLayer;

extern CTLayer* CTAddLayer(int z, int width, int height, uint32_t init_color);
extern void CTComposeLayers(uint32_t* dest, int width, int height);

// Функции работы с курсором из cursor.cc
extern "C" void CTInitCursor(int scr_w, int scr_h);
extern "C" void CTUpdateCursor();
extern "C" void CTDrawCursorOnLayer(uint32_t* layer_data, int layer_width, int layer_height);

int main() {
    // Открываем DRM-устройство (/dev/dri/card0)
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
    create.bpp = 32;
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
    
    // Устанавливаем режим с фреймбуфером
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
    // Слой фона (z = 0) – фон остаётся неизменным (черный), создаём один раз.
    CTLayer* background = CTAddLayer(0, mode.hdisplay, mode.vdisplay, 0xFF000000);
    // Слой курсора (z = 1) – обновляется каждый кадр.
    CTLayer* cursorLayer = CTAddLayer(1, mode.hdisplay, mode.vdisplay, 0x00000000);
    
    // Инициализируем курсор (загрузка изображения, открытие /dev/sysmouse)
    CTInitCursor(mode.hdisplay, mode.vdisplay);
    
    // Выделяем итоговый буфер для композиции
    int num_pixels = mode.hdisplay * mode.vdisplay;
    uint32_t* composed = (uint32_t*)malloc(num_pixels * sizeof(uint32_t));
    if (!composed) {
         perror("Ошибка выделения памяти для композиции");
         exit(EXIT_FAILURE);
    }
    
    // Главный цикл: обновление слоёв и композиция с частотой ~60 FPS
    while (1) {
         // Фон остаётся неизменным, поэтому background->data не меняется.
         // Обновляем позицию курсора и перерисовываем слой курсора.
         CTUpdateCursor();
         CTDrawCursorOnLayer(cursorLayer->data, mode.hdisplay, mode.vdisplay);
         
         // Компонуем два слоя: сначала копируем фон, затем накладываем курсор.
         CTComposeLayers(composed, mode.hdisplay, mode.vdisplay);
         
         // Копируем итоговое изображение в фреймбуфер.
         memcpy(buffer, composed, num_pixels * sizeof(uint32_t));
         
         // Задержка ~16 мс для 60 FPS.
         usleep(16000);
    }
    
    // (Код ниже не выполнится)
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
