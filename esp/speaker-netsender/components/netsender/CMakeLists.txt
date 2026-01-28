idf_component_register(SRCS "netsender.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_netif esp_http_client nvs_flash
)

GCCVERSION=$(basename $(dirname $($GXX -print-libgcc-file-name)))
export CPLUS_INCLUDE_PATH=$PREFIX/$HOST/include/c++/$GCCVERSION:$PREFIX/lib/gcc/$HOST/$GCCVERSION/include
