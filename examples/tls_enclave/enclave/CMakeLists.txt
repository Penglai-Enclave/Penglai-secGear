# Copyright (c) Huawei Technologies Co., Ltd. 2020. All rights reserved.
# secGear is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#     http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
# PURPOSE.
# See the Mulan PSL v2 for more details.

#set sign key
set(PEM Enclave_private.pem)

#set sign tool
set(SIGN_TOOL ${LOCAL_ROOT_PATH}/tools/sign_tool/sign_tool.sh)

#set enclave src code
set(SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/enclave_server.c)

#set log level
set(PRINT_LEVEL 3)
add_definitions(-DPRINT_LEVEL=${PRINT_LEVEL})

if(NOT IS_DIRECTORY ${PL_SSLLIB_PATH})
    message(FATAL_ERROR "Please provide the correct PL_SSLLIB_PATH path")
endif()

if(CC_SGX)
    #set signed output
    set(OUTPUT enclave.signed.so)
    set(AUTO_FILES  ${CMAKE_CURRENT_BINARY_DIR}/${PREFIX}_t.h ${CMAKE_CURRENT_BINARY_DIR}/${PREFIX}_t.c)
    add_custom_command(OUTPUT ${AUTO_FILES}
    DEPENDS ${CURRENT_ROOT_PATH}/${EDL_FILE}
    COMMAND ${CODEGEN} --${CODETYPE} --trusted ${CURRENT_ROOT_PATH}/${EDL_FILE} --search-path ${LOCAL_ROOT_PATH}/inc/host_inc/sgx --search-path ${SGXSDK}/include --search-path ${SSL_PATH}/include)
endif()

if(CC_PL)
    set(OUTPUT tls_penglai-ELF)
    set(AUTO_FILES  ${CMAKE_CURRENT_BINARY_DIR}/${PREFIX}_t.h ${CMAKE_CURRENT_BINARY_DIR}/${PREFIX}_t.c ${CMAKE_CURRENT_BINARY_DIR}/${PREFIX}_args.h)
    add_custom_command(OUTPUT ${AUTO_FILES}
    DEPENDS ${CURRENT_ROOT_PATH}/${EDL_FILE}
    COMMAND ${CODEGEN} --${CODETYPE} --trusted ${CURRENT_ROOT_PATH}/${EDL_FILE} --search-path ${LOCAL_ROOT_PATH}/inc/host_inc/penglai --search-path ${SSL_PATH}/include)
endif()

set(COMMON_C_FLAGS "-W -Wall -Werror  -fno-short-enums  -fno-omit-frame-pointer -fstack-protector \
	-Wstack-protector --param ssp-buffer-size=4 -frecord-gcc-switches -Wextra -nostdinc -nodefaultlibs \
	-fno-peephole -fno-peephole2 -Wno-main -Wno-error=unused-parameter \
	-Wno-error=unused-but-set-variable -Wno-error=format-truncation=")

set(COMMON_C_LINK_FLAGS "-Wl,-z,now -Wl,-z,relro -Wl,-z,noexecstack -Wl,-nostdlib -nodefaultlibs -nostartfiles")
    
if(CC_SGX)
    set(SGX_MODE HW)
    set(CMAKE_C_FLAGS "${COMMON_C_FLAGS} -m64 -fvisibility=hidden -include${SSL_PATH}/include/tsgxsslio.h")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS}  -s")

    if(${SGX_MODE} STREQUAL HW)
        set(Trts_Library_Name sgx_trts)
        set(Service_Library_Name sgx_tservice)
    else()
        set(Trts_Library_Name sgx_trts_sim)
        set(Service_Library_Name sgx_tservice_sim)
    endif()

    set(CMAKE_SHARED_LINKER_FLAGS  "${COMMON_C_LINK_FLAGS} -Wl,-z,defs -Wl,-pie -Bstatic -Bsymbolic -eenclave_entry \
	-Wl,--export-dynamic -Wl,--defsym,__ImageBase=0 -Wl,--gc-sections -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/Enclave.lds")

    if(${CMAKE_VERSION} VERSION_LESS "3.13.0") 
	link_directories(${SSL_PATH}/lib64
           ${SGXSDK}/lib64
           ${CMAKE_BINARY_DIR}/lib)
    endif()

    add_library(${PREFIX} SHARED ${SOURCE_FILES} ${AUTO_FILES})

    target_include_directories(${PREFIX} PRIVATE
	    ${CMAKE_CURRENT_BINARY_DIR}
	    ${SGXSDK}/include/tlibc
	    ${SGXSDK}/include/libcxx
	    ${SGXSDK}/include
	    ${SSL_PATH}/include
	    ${LOCAL_ROOT_PATH}/inc/host_inc
	    ${LOCAL_ROOT_PATH}/inc/host_inc/sgx
	    ${LOCAL_ROOT_PATH}/inc/enclave_inc
	    ${LOCAL_ROOT_PATH}/inc/enclave_inc/sgx
	    )
    
    if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.13.0") 
        target_link_directories(${PREFIX} PRIVATE
	   ${SSL_PATH}/lib64
	   ${SGXSDK}/lib64
	   ${CMAKE_BINARY_DIR}/lib)
    endif()

    target_link_libraries(${PREFIX} -lsecgear_tee -Wl,--whole-archive ${Trts_Library_Name} -lsgx_tsgxssl -Wl,--no-whole-archive -Wl,--start-group -lsgx_tsgxssl_ssl -lsgx_tsgxssl_crypto -lsgx_tstdc -lsgx_tcxx -lsgx_tcrypto -lsgx_pthread -l${Service_Library_Name} -Wl,--end-group)
 
    add_custom_command(TARGET ${PREFIX} 
    POST_BUILD
    COMMAND umask 0177
    COMMAND openssl genrsa -3 -out ${PEM} 3072
    COMMAND bash ${SIGN_TOOL} -d sign -x sgx -i ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/lib${PREFIX}.so -k ${PEM} -o ${OUTPUT} -c ${CMAKE_CURRENT_SOURCE_DIR}/Enclave.config.xml)
endif()

if(NOT DEFINED CC_PL)
    set_target_properties(${PREFIX} PROPERTIES SKIP_BUILD_RPATH TRUE)
endif()

if(CC_PL)
    set(SDK_LIB_DIR ${SDK_PATH}/lib)
    set(SDK_INCLUDE_DIR ${SDK_LIB_DIR}/app/include)
    set(SDK_APP_LIB ${SDK_LIB_DIR}/libpenglai-enclave-eapp.a)
    set(SDK_GM_LIB ${SDK_LIB_DIR}/libpenglai-enclave-gm.a)
    set(MUSL_LIB_DIR ${SDK_PATH}/musl/lib)
    set(MUSL_LIBC ${MUSL_LIB_DIR}/libc.a)
    set(GCC_LIB ${SDK_LIB_DIR}/libgcc.a)
    set(SECGEAR_TEE_LIB ${CMAKE_BINARY_DIR}/lib/libsecgear_tee.a)

    set(SOURCE_C_OBJS "")
    foreach(SOURCE_FILE ${SOURCE_FILES})
        STRING(REGEX REPLACE ".+/(.+)\\..*" "\\1" SOURCE_FILE_NAME ${SOURCE_FILE})
        set(SOURCE_OBJ ${CMAKE_CURRENT_BINARY_DIR}/${SOURCE_FILE_NAME}.o)
        add_custom_command(
            OUTPUT ${SOURCE_OBJ}
            DEPENDS ${SOURCE_FILES}
            COMMAND gcc -Wall -I${SDK_INCLUDE_DIR} -I${CMAKE_CURRENT_BINARY_DIR} -I${CMAKE_BINARY_DIR}/inc
                -I${LOCAL_ROOT_PATH}/inc/host_inc -I${LOCAL_ROOT_PATH}/inc/host_inc/penglai -I${LOCAL_ROOT_PATH}/inc/enclave_inc
                -I${LOCAL_ROOT_PATH}/inc/enclave_inc/penglai -I ${CMAKE_CURRENT_SOURCE_DIR}/../penglai_calcu_lib/libpenglai_calcu_lib/calculate_lib -c -o ${SOURCE_OBJ} ${SOURCE_FILE}
                -I${SSL_PATH}/include
            COMMENT "generate SOURCE_OBJ"
        )
        list(APPEND SOURCE_C_OBJS ${SOURCE_OBJ})
    endforeach()

    set(APP_C_OBJ ${CMAKE_CURRENT_BINARY_DIR}/${PREFIX}_t.o)
    add_custom_command(
        OUTPUT ${APP_C_OBJ}
        DEPENDS ${AUTO_FILES}
        COMMAND gcc -Wall -I${SDK_INCLUDE_DIR} -I${CMAKE_CURRENT_BINARY_DIR} -I${CMAKE_BINARY_DIR}/inc
            -I${LOCAL_ROOT_PATH}/inc/host_inc -I${LOCAL_ROOT_PATH}/inc/host_inc/penglai -I${LOCAL_ROOT_PATH}/inc/enclave_inc
            -I${LOCAL_ROOT_PATH}/inc/enclave_inc/penglai -c -o ${APP_C_OBJ} ${CMAKE_CURRENT_BINARY_DIR}/${PREFIX}_t.c
        COMMENT "generate APP_C_OBJ"
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/${OUTPUT}
        DEPENDS ${APP_C_OBJ} ${SOURCE_C_OBJS} ${SDK_APP_LIB} ${SDK_GM_LIB} ${MUSL_LIBC} ${GCC_LIB}
        COMMAND ld -static -L${CMAKE_LIBRARY_OUTPUT_DIRECTORY} -L${SDK_LIB_DIR} -L${MUSL_LIB_DIR} -L/usr/lib64
            -L${PL_SSLLIB_PATH}/lib64 -lsgx_tsgxssl_crypto -lsgx_tsgxssl_ssl -lsgx_tsgxssl
            -lpenglai-enclave-eapp -lpenglai-enclave-gm -lsecgear_tee -lc
            -o ${CMAKE_CURRENT_SOURCE_DIR}/${OUTPUT} ${APP_C_OBJ} ${SOURCE_C_OBJS} --start-group ${SECGEAR_TEE_LIB} ${SDK_APP_LIB} ${SDK_GM_LIB}
            ${PL_SSLLIB_PATH}/lib64/libsgx_tsgxssl_ssl.a ${PL_SSLLIB_PATH}/lib64/libsgx_tsgxssl_crypto.a ${PL_SSLLIB_PATH}/lib64/libsgx_tsgxssl.a
            ${MUSL_LIBC} ${GCC_LIB} --end-group -T ${SDK_PATH}/app.lds
        COMMAND chmod -x ${CMAKE_CURRENT_SOURCE_DIR}/${OUTPUT}
        COMMENT "generate penglai-ELF"
    )
    add_custom_target(
        ${OUTPUT} ALL
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${OUTPUT}
        COMMENT "makefile target penglai-ELF"
    )

endif()
