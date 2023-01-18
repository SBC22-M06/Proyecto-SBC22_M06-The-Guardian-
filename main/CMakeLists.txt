idf_component_register(SRCS "ssd1306_i2c.c" "ssd1306_spi.c" "ssd1306.c" "max30100.c" "app_main.c"
                    INCLUDE_DIRS "."
		    EMBED_TXTFILES telegram_certificate.pem)
