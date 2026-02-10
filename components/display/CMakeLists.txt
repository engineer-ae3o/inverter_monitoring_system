idf_component_register (
                        SRCS "display.cpp"
                        INCLUDE_DIRS "."
                        REQUIRES ili9341 lvgl config utils screens alert system
)
