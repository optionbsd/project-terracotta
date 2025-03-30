# Компилятор
CXX := clang++

# Флаги компиляции
CXXFLAGS := -std=c++20 -Wall -Wextra -O2 -I/usr/local/include -I/usr/local/include/libdrm

# Флаги линковки
LDFLAGS := -L/usr/local/lib -ldrm

# Директории
SRC_DIR := sources
BUILD_DIR := build
OUTPUT_DIR := $(BUILD_DIR)/.output
RES_DIR := $(SRC_DIR)/res
TARGET := $(BUILD_DIR)/terracotta

# Поиск всех файлов с исходниками
SOURCES := $(wildcard $(SRC_DIR)/*.cc)
OBJECTS := $(patsubst $(SRC_DIR)/%.cc, $(OUTPUT_DIR)/%.o, $(SOURCES))

# Цель по умолчанию
all: $(TARGET) copy_res

# Компиляция объектных файлов
$(OUTPUT_DIR)/%.o: $(SRC_DIR)/%.cc | $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Создание итогового исполняемого файла
$(TARGET): $(OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

# Копирование папки res
copy_res: | $(BUILD_DIR)
	@cp -r $(RES_DIR) $(BUILD_DIR)/

# Создание директорий, если их нет
$(OUTPUT_DIR):
	@mkdir -p $@

$(BUILD_DIR):
	@mkdir -p $@

# Очистка билда
clean:
	@rm -rf $(BUILD_DIR)

.PHONY: all copy_res clean
