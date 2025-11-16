cmake_minimum_required(VERSION 3.29)
project(repa_server C)

set(CMAKE_C_STANDARD 23)

# Ищем glib-2.0 через pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)


#add_compile_options(
#        -O3
#)

add_compile_options(
        -fsanitize=undefined,thread
        -fno-omit-frame-pointer
        -g
        -O1
)
add_link_options(
        -fsanitize=undefined,thread
        -fno-omit-frame-pointer
)

add_executable(repa_server main.c
        network/master_listener.c
        network/master_listener.h
        logger.c
        network/router.h
        network/router.c
        repa_client.h
        work/workers_pool.h
        work/workers_pool.c
        resp_parser.c
        network/net_client.c
        network/net_client.h
        db/command_processor.c
        db/command_processor.h
        resp_serializer.c
        resp_serializer.h
        db/db_backend.c
        db/db_backend.h
        db/db_services.c
        db/db_services.h
        db/stats_service.c
        db/stats_service.h
        util/config.c
        util/exception.c
        util/config_parser.c
        util/utils.c
        util/config_extend.h
        util/config_extend.c
        signal_handler.c
        signal_handler.h
        signal_handler.c
        coroutine_pool/coroutine_pool.h
        coroutine_pool/coroutine_pool.c
        coroutine_pool/coro_test.c)

# Добавляем include пути и линковку
target_include_directories(repa_server PRIVATE ${GLIB_INCLUDE_DIRS})
target_link_libraries(repa_server PRIVATE ${GLIB_LIBRARIES})
target_compile_options(repa_server PRIVATE ${GLIB_CFLAGS_OTHER})
target_compile_definitions(repa_server PRIVATE _GNU_SOURCE)

set(CONF "repa.conf")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/${CONF}
        ${CMAKE_CURRENT_BINARY_DIR}/${CONF}
        COPYONLY)