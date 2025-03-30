#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <stdint.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

int main() {
    // Открываем DRM устройство (обычно /dev/dri/card0)
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
    drmModeConnector *connector = nullptr;
    uint32_t connector_id = 0;
    for (int i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(fd, resources->connectors[i]);
        if (connector && connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0) {
            connector_id = connector->connector_id;
            break;
        }
        drmModeFreeConnector(connector);
        connector = nullptr;
    }
    if (!connector) {
        fprintf(stderr, "Не найден подходящий коннектор\n");
        drmModeFreeResources(resources);
        close(fd);
        return EXIT_FAILURE;
    }

    // Выбираем первый доступный режим (как пример)
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
        // Не забываем очистить созданный dumb buffer
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

    // Заполняем буфер белым цветом (0xffffffff для ARGB8888)
    memset(buffer, 0xff, size);

    // Переключаем на выбранный режим с нашим фреймбуфером
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

    // Оставляем белый экран видимым в течение 5 секунд
    for (;;) {
        sleep(1);
    }

    // Освобождаем ресурсы
    munmap(buffer, size);
    drmModeRmFB(fd, fb);
    struct drm_mode_destroy_dumb destroy = {};
    destroy.handle = handle;
    drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);
    close(fd);
    return EXIT_SUCCESS;
}
