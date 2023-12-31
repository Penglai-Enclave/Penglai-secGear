/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2020. All rights reserved.
 * secGear is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <termios.h>
#include "openssl/evp.h"
#include "openssl/x509.h"
#include "openssl/pem.h"
#include "openssl/ssl.h"
#include "tls_enclave_u.h"
#include "enclave.h"

#define BUF_LEN 1024
#define MAX_LISTEN_FD 64
#define PASS_MAX 32
#define MAX_ENC_KEY_LEN 4096
#define ENC_KEY_FILE_NAME "enc_key"
#define REPORT_SIZE 328
#define PLENCLAVE_SIZE 424

void printHexsecGear(unsigned char *c, int n)
{
    int m = n / 16;
    int left = n - m * 16;
    char buf[33] = {0};
    char num;
    int top, below;
    printf("n: %d, m: %d, left: %d\n", n, m, left);
    for(int j = 0; j < m; j++){
        for(int i = 0; i < 16; i++){
            num = *(c + j*16 + i);
            top = (num >> 4) & 0xF;
            below = num & 0xF;
            buf[2 * i] = (top < 10 ? '0'+top : 'a'+top-10);
            buf[2 * i + 1] = (below < 10 ? '0'+below : 'a'+below-10);
        }
        buf[32] = '\0';
        printf("%d - %d: %s\n", j*16, j*16+15, buf);
    }
	if(left != 0){
        for(int i = 0; i < left; i++){
            num = *(c + m*16 + i);
            top = (num >> 4) & 0xF;
            below = num & 0xF;
            buf[2 * i] = (top < 10 ? '0'+top : 'a'+top-10);
            buf[2 * i + 1] = (below < 10 ? '0'+below : 'a'+below-10);
        }
        buf[2 * left] = '\0';
        printf("%d - %d: %s\n", m*16, m*16+left-1, buf);
    }
}

int set_echo_mode(int fd, int option)
{
    struct termios term;
    if (tcgetattr(fd, &term) != 0) {
        return CC_FAIL;
    }
    if (option) {
        term.c_lflag |= (ECHO | ECHOE | ECHOK | ECHONL);
    } else {
        term.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
    }
    if (tcsetattr(fd, TCSAFLUSH, &term) != 0) {
        return CC_FAIL;
    }
    return CC_SUCCESS;
}

int get_password_and_seal_key(cc_enclave_t *context, const char *key_file_name, const char *enc_key_file_name)
{
    int res = CC_FAIL;
    size_t retval = 0;
    size_t pw_len = 0;
    char password[PASS_MAX] = {0};
    char *enc_key = NULL;
    FILE *fp = NULL;

    printf("Please input password:\n");
    if (set_echo_mode(STDIN_FILENO, 0)) {
        return CC_FAIL;
    }
    if (fgets((char *)password, PASS_MAX, stdin) == NULL) {
        return CC_FAIL;
    }
    pw_len = strlen((const char *)password);
    if (password[pw_len - 1] == '\n') {
        password[pw_len-1] = 0;
        pw_len--;
    }
    if (set_echo_mode(STDIN_FILENO, 1)) {
        goto end;
    }
    enc_key = malloc(MAX_ENC_KEY_LEN);
    if (enc_key == NULL) {
        goto end;
    }
    res = seal_key(context, &retval, key_file_name, strlen(key_file_name) + 1, password, pw_len + 1, 
                   enc_key, MAX_ENC_KEY_LEN);
    if (res != CC_SUCCESS || retval == 0) {
        res = CC_FAIL;
        goto end;
    }
    fp = fopen(enc_key_file_name, "w+");
    if (fp == NULL) {
        res = CC_FAIL;
        goto end;
    }
    printf("After seal_key in host, enc_key length: %ld, content:\n", retval);
    // printHexsecGear((unsigned char *)enc_key, retval);
    if (fwrite(enc_key, sizeof(char), retval, fp) != retval) {
        fclose(fp);
        res = CC_FAIL;
        goto end;
    }
    fclose(fp);
    // if (remove(key_file_name) == 0) {
    //     printf("delete origin key file success!\n");
    // } else {
    //     printf("delete origin key file error!\n");
    //     res = CC_FAIL;
    // }

end:
    memset(password, 0, pw_len);
    return res;
}

int start_server(int port)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        return -1;
    }
    listen(server_fd, MAX_LISTEN_FD);
    return server_fd;
}

int main(int argc, const char *argv[])
{
    char *path = PATH;
    cc_enclave_t context_data = {0};
    cc_enclave_t *context = &context_data;
    struct sockaddr_in client_addr;
    socklen_t client_len;
    int server_fd = -1;
    int tlsc_fd = -1;
    cc_enclave_result_t res = CC_FAIL;
    int retval = 0;
    char report[REPORT_SIZE] = {0};

    if (argc != 4) {
        printf("usage: %s port cert_file key_file\n", argv[0]);
        return CC_FAIL;
    }

    server_fd = start_server(atoi(argv[1]));
    if (server_fd < 0) {
        return CC_FAIL;
    } 
    tlsc_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
    if (tlsc_fd < 0) {
        close(server_fd);
        return CC_FAIL;
    }
    printf("Create secgear enclave\n");
    res = cc_enclave_create(path, AUTO_ENCLAVE_TYPE, 0, SECGEAR_DEBUG_FLAG, NULL, 0, context);
    if (res != CC_SUCCESS) {
        printf("Create enclave error\n");
        close(tlsc_fd);
        close(server_fd);
        return CC_FAIL;
    }
    printf("Create secgear enclave success\n");
#ifdef ENABLE_ENC_KEY
    res = get_password_and_seal_key(context, argv[3], ENC_KEY_FILE_NAME);
    if (res !=  CC_SUCCESS) {
        printf("get_password_and_seal_key error\n");
        goto end;
   }
#endif
    memcpy(report, (char*)(context->private_data) + PLENCLAVE_SIZE-REPORT_SIZE, REPORT_SIZE);
    // 打印读取到的数据
    printf("begin print report:");
    printHexsecGear(report,REPORT_SIZE);

    // 传入report地址和大小
    res = start_enclave_tls(context, &retval, tlsc_fd, argv[2], strlen(argv[2]) + 1, ENC_KEY_FILE_NAME, 
                            strlen(ENC_KEY_FILE_NAME) + 1,report,REPORT_SIZE+1);
    if (res !=  CC_SUCCESS || retval !=  CC_SUCCESS) {
        printf("start_enclave_tls error\n");			        
        goto end;
    }

    printf("enclve tls finish\n");

end:
    res = cc_enclave_destroy(context);
    if(res != CC_SUCCESS) {
        printf("Destroy enclave error\n");
    }
    close(tlsc_fd);
    close(server_fd);
    return res;
}
