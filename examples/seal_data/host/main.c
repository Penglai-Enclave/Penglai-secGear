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
#include "enclave.h"
#include "seal_data_u.h"

#define BUF_LEN 32
#define IN_LEN 16

int main()
{
    int  retval = 0;
    char *path = PATH;
    char buf[BUF_LEN];
    char in[IN_LEN];
    cc_enclave_result_t res;
    cc_enclave_t context = {0};
    printf("Create secgear enclave\n");
    res = cc_enclave_create(path, AUTO_ENCLAVE_TYPE, 0, SECGEAR_DEBUG_FLAG, NULL, 0, &context);
    if (res != CC_SUCCESS) {
        printf("Create enclave error\n");
        return res;
    }
    const char *str = "abcd123";  // 要赋值的字符串
    int i;
    for (i = 0; i < IN_LEN - 1 && str[i] != '\0'; ++i) {
        in[i] = str[i];
    }
    in[i] = '\0';  // 确保在数组末尾添加空字符
    res = seal_data_test_func(&context, &retval, in, 16, buf, IN_LEN);
    if (res != CC_SUCCESS || retval != (int)CC_SUCCESS) {
        printf("Ecall enclave error\n");
    } else {
        printf("%s\n", buf);
    }

    res = cc_enclave_destroy(&context);
    if(res != CC_SUCCESS) {
        printf("Destroy enclave error\n");
    }
    return res;
}
