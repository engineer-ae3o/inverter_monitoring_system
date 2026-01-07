idf_component_register (
                        SRCS "display.cpp"
                        INCLUDE_DIRS "."
                        PRIV_REQUIRES main st7735 ili9341 lvgl config utils screens alert system
)
