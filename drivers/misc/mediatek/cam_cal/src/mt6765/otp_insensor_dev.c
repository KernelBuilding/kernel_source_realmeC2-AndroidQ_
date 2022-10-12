/***********************************************************
 * ** Copyright (C), 2008-2016, OPPO Mobile Comm Corp., Ltd.
 * ** ODM_HQ_EDIT
 * ** File: - otp_insensor_dev.c
 * ** Description: Source file for CBufferList.
 * **           To allocate and free memory block safely.
 * ** Version: 1.0
 * ** Date : 2018/12/07
 * ** Author: YanKun.Zhai@Mutimedia.camera.driver.otp
 * **
 * ** ------------------------------- Revision History: -------------------------------
 * **   <author>History<data>      <version >version       <desc>
 * **  YanKun.Zhai 2018/12/07     1.0     build this module
 * **
 * ****************************************************************/

#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include <linux/slab.h>

#include "cam_cal.h"
#include "cam_cal_define.h"
#include "cam_cal_list.h"
#include "eeprom_i2c_dev.h"

#include "kd_camera_typedef.h"
//#include "kd_camera_hw.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"

#define LOG_TAG "OTP_InSensor"
#define LOG_ERR(format,...) pr_err(LOG_TAG "Line:%d  FUNC: %s:  "format,__LINE__,__func__,## __VA_ARGS__)
#define LOG_INFO(format,...) pr_info(LOG_TAG "  %s:  "format,__func__,## __VA_ARGS__)
#define LOG_DEBUG(format,...) pr_debug(LOG_TAG "  %s:  "format,__func__,## __VA_ARGS__)

#define ERROR_I2C       1
#define ERROR_CHECKSUM  2
#define ERROR_READ_FLAG 3




static DEFINE_SPINLOCK(g_spinLock);

static struct i2c_client *g_pstI2CclientG;
/* add for linux-4.4 */
#ifndef I2C_WR_FLAG
#define I2C_WR_FLAG		(0x1000)
#define I2C_MASK_FLAG	(0x00ff)
#endif

extern struct i2c_client *g_pstI2Cclients[I2C_DEV_IDX_MAX] ;


#define MAX_EEPROM_BYTE 0x1FFF
#define CHECKSUM_FLAG_ADDR MAX_EEPROM_BYTE-1
#define READ_FLAG_ADDR MAX_EEPROM_BYTE-2

#define OTP_DATA_GOOD_FLAG 0x88
char g_otp_buf[3][MAX_EEPROM_BYTE] = {{0},{0},{0}};




#define MAX_NAME_LENGTH 20
#define MAX_GROUP_NUM 8
#define MAX_GROUP_ADDR_NUM 3

typedef struct
{
    int group_flag_page;
    int group_flag_addr;
    int group_flag;
    int group_start_page;
    int group_start_addr;
    int group_end_page;
    int group_end_addr;
    int group_checksum_page;
    int group_checksum_addr;
}GROUP_ADDR_INFO;

typedef struct
{
    char group_name[MAX_NAME_LENGTH];
    int group_flag_page;
    int group_flag_addr;
    GROUP_ADDR_INFO group_addr_info[MAX_GROUP_ADDR_NUM];
}GROUP_INFO;


typedef struct
{
    char module_name[MAX_NAME_LENGTH];
    int is_page_read;
    int page_start_addr;
    int page_end_addr;
    int group_num;
    int group_addr_info_num;
    int group_checksum;
    GROUP_INFO group_info[MAX_GROUP_NUM];
    int  (* readFunc) (u8 page,u16 addr, u8 *data);
}OTP_MAP;


int hi556_read_data(u8 page,u16 addr,u8 *data);
int s5k3h7yx_read_data(u8 page,u16 addr,u8 *data);
int gc5035_read_data(u8 page,u16 addr,u8 *data);



OTP_MAP hi556_otp_map = {
        .module_name = "HI556_LY",
        .is_page_read = 0,
        .group_num = 4,
        .group_addr_info_num = 3,
        .readFunc = hi556_read_data,
        .group_checksum = 0,
        .group_info = {
                        {"info",
                            -1,0x0401,
                              {
                                 {-1,-1,0x01,-1,0x402,-1,0x409,-1,0x412},
                                 {-1,-1,0x13,-1,0x413,-1,0x41A,-1,0x423},
                                 {-1,-1,0x37,-1,0x424,-1,0x42B,-1,0x434},

                              },
                        },
                        {"awb",
                            -1,0x0435,
                            {
                                {-1,-1,0x01,-1,0x436,-1,0x441,-1,0x453},
                                {-1,-1,0x13,-1,0x454,-1,0x45F,-1,0x471},
                                {-1,-1,0x37,-1,0x472,-1,0x47E,-1,0x48F},
                             },
                        },
                        {"lightsource",
                            -1,0x04c0,
                            {
                                {-1,-1,0x01,-1,0x4C1,-1,0x4C4,-1,0x4C5},
                                {-1,-1,0x13,-1,0x4c6,-1,0x4c9,-1,0x4CA},
                                {-1,-1,0x37,-1,0x4cb,-1,0x4Ce,-1,0x4CF},
                            },
                        },
                        {"lsc",
                            -1,0x04D0,
                            {
                                {-1,-1,0x01,-1,0x4D1, -1, 0xC1C, -1, 0xC1D},
                                {-1,-1,0x13,-1,0xC1E, -1, 0x1369,-1, 0x136A},
                                {-1,-1,0x37,-1,0x136B,-1, 0x1AB6,-1, 0x1AB7},
                            },
                        },
            },
};
OTP_MAP s5k3h7yx_otp_map = {
        .module_name = "S5K3H7YX_LY",
        .is_page_read = 1,
        .page_start_addr = 0x0A04,
        .page_end_addr = 0x0A43,
        .group_num = 4,
        .group_addr_info_num = 2,
        .readFunc = s5k3h7yx_read_data,
        .group_checksum = 0,
        .group_info = {
                        {"info_awb",
                            -1,-1,
                              {
                                {0x01,0x0A04,0x01,0x01,0x0A05,0x01,0x0A1A,0x01,0x0A1B},
                                {0x02,0x0A04,0x01,0x02,0x0A05,0x02,0x0A1A,0x02,0x0A1B},
                              },
                        },
                        {"lightsource",
                            -1,-1,
                            {
                                {0x01,0x0A1C,0x01,0x01,0x0A1D,0x01,0x0A20,0x01,0x0A21},
                                {0x02,0x0A1C,0x01,0x02,0x0A1D,0x02,0x0A20,0x02,0x0A21},

                             },
                        },
                        {"af",
                            -1,-1,
                            {
                                 {0x01,0x0A22,0x01,0x01,0x0A23,0x01,0x0A26,0x01,0x0A27},
                                 {0x02,0x0A22,0x01,0x02,0x0A23,0x02,0x0A26,0x02,0x0A27},

                            },
                        },
                        {"lsc",
                            -1,-1,
                            {
                                 {0x0C,0x0A18,0x01,0x04,0x0A04,0x08,0x0A0C,0x0C,0x0A19},
                                 {0x0C,0x0A16,0x01,0x08,0x0A0D,0x0C,0x0A15,0x0C,0x0A17},
                            },
                        },
            },
};

OTP_MAP gc5035_otp_map = {
        .module_name = "GC5035_JK",
        .is_page_read = 0,
        .group_num = 3,
        .group_addr_info_num = 1,
        .readFunc = gc5035_read_data,
        .group_checksum = 1,
        .group_info = {
                        {"info",
                            -1,0x00,
                              {
                                 {-1,-1,0x01,-1,0x01,-1,0x16,-1,0x17},
                              },
                        },
                        {"awb",
                            -1,0x19,
                            {
                                {-1,-1,0x01,-1,0x1A,-1,0x29,-1,0x2A},
                             },
                        },
                        {"lsc",
                            -1,0x34,
                            {
                                {-1,-1,0x01,-1,0x35, -1, 0x780, -1, 0x781},
                            },
                        },
            },
};

OTP_MAP gc5035_ofg_otp_map = {
        .module_name = "GC5035_OFG",
        .is_page_read = 0,
        .group_num = 3,
        .group_addr_info_num = 1,
        .readFunc = gc5035_read_data,
        .group_checksum = 2,
        .group_info = {
                        {"info",
                            -1,0x00,
                              {
                                 {-1,-1,0x01,-1,0x01,-1,0x08,-1,0x0C},
                              },
                        },
                        {"awb",
                            -1,0x0D,
                            {
                                {-1,-1,0x01,-1,0x0E,-1,0x1D,-1,0x20},
                             },
                        },
                        {"lsc",
                            -1,0x21,
                            {
                                {-1,-1,0x01,-1,0x22, -1, 0x76D, -1, 0x76E},
                            },
                        },
            },
};


OTP_MAP s5k3h7yx_txd_otp_map = {
        .module_name = "S5K3H7YX_TXD",
        .is_page_read = 1,
        .page_start_addr = 0x0A04,
        .page_end_addr = 0x0A43,
        .group_num = 6,
        .group_addr_info_num = 2,
        .readFunc = s5k3h7yx_read_data,
        .group_checksum = 0,
        .group_info = {
                        {"info",
                            1,0x0A05,
                            {
                                 {-1,-1,0x01,0x02,0x0A04,0x02,0x0A0F,0x02,0x0A10},
                                 {-1,-1,0x03,0x03,0x0A04,0x03,0x0A0F,0x03,0x0A10},

                            },
                        },
                        {"awb",
                            1,0x0A06,
                            {
                                 {-1,-1,0x01,0x02,0x0A11,0x02,0x0A24,0x02,0x0A25},
                                 {-1,-1,0x03,0x03,0x0A11,0x03,0x0A24,0x03,0x0A25},
                            },
                        },
                        {"lightsource",
                            1,0x0A07,
                            {
                                 {-1,-1,0x01,0x02,0x0A26,0x02,0x0A29,0x02,0x0A2A},
                                 {-1,-1,0x03,0x03,0x0A26,0x03,0x0A29,0x03,0x0A2A},
                            },
                        },
                        {"af",
                            1,0x0A08,
                            {
                                 {-1,-1,0x01,0x01,0x0A09,0x01,0x0A0C,0x01,0x0A0D},
                                 {-1,-1,0x03,0x01,0x0A0E,0x01,0x0A11,0x01,0x0A12},
                            },
                        },
                        {"lsc",
                            0,0x0A33,
                              {
                                {-1,-1,0x01,0x04,0x0A04,0x08,0x0A0C,0x02,0x0A2B},
                                {-1,-1,0x03,0x08,0x0A0D,0x0C,0x0A15,0x03,0x0A2B},
                              },
                        },
                        {"SN",
                            1,0x0A04,
                            {
                                {-1,-1,0x01,0x01,0x0A13,0x01,0x0A23,0x01,0x0A28},
                                {-1,-1,0x03,0x01,0x0A29,0x01,0x0A39,0x01,0x0A3E},

                             },
                        },
            },
};

OTP_MAP s5k3h7yx_ts_otp_map = {
        .module_name = "S5K3H7YX_TS",
        .is_page_read = 1,
        .page_start_addr = 0x0A04,
        .page_end_addr = 0x0A43,
        .group_num = 4,
        .group_addr_info_num = 2,
        .readFunc = s5k3h7yx_read_data,
        .group_checksum = 0,
        .group_info = {
                        {"info_awb",
                            1,0x0a04,
                            {
                                 {-1,-1,0x40,0x01,0x0a05,0x01,0x0a1b,0x01,0x0a1c},
                                 {-1,-1,0xd0,0x01,0x0a1d,0x01,0x0a33,0x01,0x0a34},
                            },
                        },
                        {"lightsource",
                            2,0x0a04,
                            {
                                 {-1,-1,0x40,0x02,0x0a05,0x02,0x0a08,0x02,0x0a09},
                                 {-1,-1,0xd0,0x02,0x0a0a,0x02,0x0a0d,0x02,0x0a0e},
                            },
                        },
                        {"af",
                            1,0x0a38,
                            {
                                 {-1,-1,0x40,0x01,0x0a39,0x01,0x0a3c,0x01,0x0a3d},
                                 {-1,-1,0xd0,0x01,0x0a3e,0x01,0x0a41,0x01,0x0a42},
                            },
                        },
                        {"lsc",
                            1,0x0a35,
                              {
                                {-1,-1,0x40,0x04,0x0a04,0x08,0x0a0c,0x01,0x0a36},
                                {-1,-1,0xd0,0x08,0x0a0d,0x0C,0x0a15,0x01,0x0a37},
                              },
                        },
            },
};

static int read_reg16_data8(u16 addr, u8 *data)
{
    int  i4RetValue = 0;
    char puSendCmd[2] = {(char)(addr >> 8), (char)(addr & 0xff)};

    spin_lock(&g_spinLock);
        g_pstI2CclientG->addr =
            g_pstI2CclientG->addr & (I2C_MASK_FLAG | I2C_WR_FLAG);
    spin_unlock(&g_spinLock);

    i4RetValue = i2c_master_send(g_pstI2CclientG,
        puSendCmd, 2);
    if (i4RetValue != 2) {
        pr_err("I2C send failed!!, Addr = 0x%x\n", addr);
        return -1;
    }
    i4RetValue = i2c_master_recv(g_pstI2CclientG,
        (char *)data, 1);
    if (i4RetValue != 1) {
        pr_err("I2C read failed!!\n");
        return -1;
    }
    spin_lock(&g_spinLock);
    g_pstI2CclientG->addr = g_pstI2CclientG->addr & I2C_MASK_FLAG;
    spin_unlock(&g_spinLock);

    return 0;
}
static int write_reg16_data8(u16 addr, u8 data)
{

    int  i4RetValue = 0;
    char puSendCmd[3] = {(char)(addr >> 8), (char)(addr & 0xff),
                        (char)(data & 0xff)};
    spin_lock(&g_spinLock);
    g_pstI2CclientG->addr =
            g_pstI2CclientG->addr & (I2C_MASK_FLAG | I2C_WR_FLAG);
    spin_unlock(&g_spinLock);


    i4RetValue = i2c_master_send(g_pstI2CclientG, puSendCmd, 3);

    if (i4RetValue != 3) {
        pr_err("I2C send failed!!, Addr = 0x%x\n",addr);
        return -1;
    }
    spin_lock(&g_spinLock);
    g_pstI2CclientG->addr = g_pstI2CclientG->addr & I2C_MASK_FLAG;
    spin_unlock(&g_spinLock);

    return 0;
}

static int write_reg16_data16(u16 addr, u16 data)
{
    int  i4RetValue = 0;
    char puSendCmd[4] = {(char)(addr >> 8), (char)(addr & 0xff),
                        (char)(data >> 8),(char)(data & 0xff)};

    spin_lock(&g_spinLock);
    g_pstI2CclientG->addr =
        g_pstI2CclientG->addr & (I2C_MASK_FLAG | I2C_WR_FLAG);
    spin_unlock(&g_spinLock);

    i4RetValue = i2c_master_send(g_pstI2CclientG, puSendCmd, 4);

    if (i4RetValue != 4) {
        pr_err("I2C send failed!!, Addr = 0x%x\n",addr);
        return -1;
    }
    spin_lock(&g_spinLock);
    g_pstI2CclientG->addr = g_pstI2CclientG->addr & I2C_MASK_FLAG;
    spin_unlock(&g_spinLock);

    return 0;
}

static int parse_otp_map_data(OTP_MAP * map,char * data)
{
    int i = 0, j = 0;
    int addr = 0,size = 0,page = 0, curr_addr = 0;
    int  ret = 0;
    char readByte = 0;
    char readByte2 = 0;
    int readByte_checksum = 0;
    int checksum = -1;

    LOG_INFO("module: %s  ......",map->module_name);

    for( i = 0 ; i < map->group_num; i++){

        checksum = 0;
        size = 0;

        LOG_INFO("groupinfo: %s,start_addr 0x%04x(%04d)",map->group_info[i].group_name,curr_addr,curr_addr);

        if( map->group_info[i].group_flag_addr >= 0 ){

            ret = map->readFunc(map->group_info[i].group_flag_page,map->group_info[i].group_flag_addr,&readByte);
            if(ret < 0){
                LOG_ERR("   read flag error addr 0x%04x",map->group_info[i].group_flag_addr);
                return -ERROR_I2C;
            }
            if((0 == map->group_info[i].group_flag_page) && (0x0A33 == map->group_info[i].group_flag_addr)){
                LOG_INFO("special txd s5k3h7yx read lsc flag %d",readByte);
                readByte = readByte & 0x03;
            }
            for( j = 0; j < map->group_addr_info_num ; j++){

                if(readByte == map->group_info[i].group_addr_info[j].group_flag){
                    LOG_INFO("   found flag ,group index %d flag %d", j, readByte);
                    break;
                }
            }
        }else if((map->is_page_read) && (map->group_info[i].group_flag_addr < 0)){

            for( j = 0; j < map->group_addr_info_num ; j++){
                ret = map->readFunc(map->group_info[i].group_addr_info[j].group_flag_page,
                                        map->group_info[i].group_addr_info[j].group_flag_addr,
                                            &readByte);
                if(ret < 0){
                     LOG_ERR("   read flag error p:0x%x addr 0x%04x",
                                   map->group_info[i].group_addr_info[j].group_flag_page,
                                       map->group_info[i].group_addr_info[j].group_flag_addr);
                 }
                if(readByte == map->group_info[i].group_addr_info[j].group_flag){
                     LOG_INFO("   found flag ,group index %d flag %d", j, readByte);
                     break;
                 }
             }
        }

        if(j ==  map->group_addr_info_num){

            LOG_ERR("   Can't found group flag 0x%x",readByte);
            return -ERROR_READ_FLAG;
        }

        if(!map->is_page_read){
            for( addr = map->group_info[i].group_addr_info[j].group_start_addr;
                    addr <= map->group_info[i].group_addr_info[j].group_end_addr;
                        addr++ ){

                ret = map->readFunc(-1,addr,data);
                if(ret < 0){
                    LOG_ERR("   read data  error");
                }
                LOG_DEBUG("    group: %s, addr: 0x%04x, viraddr: 0x%04x(%04d),  data: 0x%04x(%04d) ",
                            map->group_info[i].group_name,addr,curr_addr,curr_addr,*data,*data);

                checksum += *data;
                curr_addr++;
                size++;
                data++;
            }
        }else if(1 == map->is_page_read){
            page = map->group_info[i].group_addr_info[j].group_start_page;

            if( page == map->group_info[i].group_addr_info[j].group_end_page){

                for(addr = map->group_info[i].group_addr_info[j].group_start_addr;
                        addr <= map->group_info[i].group_addr_info[j].group_end_addr;
                            addr++){

                    ret = map->readFunc(page,addr,data);
                    if(ret < 0){
                        LOG_ERR("   read data  error");
                    }
                    LOG_DEBUG("    group: %s,page %d addr: 0x%04x, viraddr: 0x%04x(%04d),  data: 0x%04x(%04d) ",
                                       map->group_info[i].group_name,page,addr,curr_addr,curr_addr,*data,*data);

                    checksum += *data;
                    curr_addr++;
                    size++;
                    data++;
                }

            }else{
                page = map->group_info[i].group_addr_info[j].group_start_page;
                for( addr = map->group_info[i].group_addr_info[j].group_start_addr;
                        addr <= map->page_end_addr;
                         addr++){

                    ret = map->readFunc(page,addr,data);
                    if(ret < 0){
                        LOG_ERR("   read data  error");
                    }
                    LOG_DEBUG("    group: %s,page %d addr: 0x%04x, viraddr: 0x%04x(%04d),  data: 0x%04x(%04d) ",
                                  map->group_info[i].group_name,page,addr,curr_addr,curr_addr,*data,*data);

                    checksum += *data;
                    curr_addr++;
                    size++;
                    data++;
                }

                for(page++;page < map->group_info[i].group_addr_info[j].group_end_page;page++){

                    for( addr = map->page_start_addr;
                            addr <= map->page_end_addr;
                              addr++){

                        ret = map->readFunc(page,addr,data);
                        if(ret < 0){
                            LOG_ERR("   read data  error");
                        }
                        LOG_DEBUG("    group: %s,page %d addr: 0x%04x, viraddr: 0x%04x(%04d),  data: 0x%04x(%04d) ",
                           map->group_info[i].group_name,page,addr,curr_addr,curr_addr,*data,*data);

                        checksum += *data;
                        curr_addr++;
                        size++;
                        data++;
                    }

                }

                page = map->group_info[i].group_addr_info[j].group_end_page;
                for( addr = map->page_start_addr;
                        addr <=  map->group_info[i].group_addr_info[j].group_end_addr;
                         addr++){
                    ret = map->readFunc(page,addr,data);
                    if(ret < 0){
                        LOG_ERR("   read data  error");
                    }
                    LOG_DEBUG("    group: %s,page %d addr: 0x%04x, viraddr: 0x%04x(%04d),  data: 0x%04x(%04d) ",
                         map->group_info[i].group_name,page,addr,curr_addr,curr_addr,*data,*data);

                    checksum += *data;
                    curr_addr++;
                    size++;
                    data++;

                }

            }
        }/*map->is_page_read*/

       if(map->group_checksum == 0){
            checksum = (checksum % 0xFF )+1;
            ret = map->readFunc(map->group_info[i].group_addr_info[j].group_checksum_page,
                                map->group_info[i].group_addr_info[j].group_checksum_addr,
                                    &readByte);
            if(checksum == readByte){
                LOG_INFO("groupinfo: %s, checksum OK c(%04d) r(%04d)",map->group_info[i].group_name,checksum,readByte);
            }else{
                LOG_ERR("groupinfo: %s, checksum ERROR ret=%d, checksum=%04d readByte=%04d",map->group_info[i].group_name,ret,checksum,readByte);
                ret = -ERROR_CHECKSUM;
                }
       }else if(map->group_checksum == 2){
            checksum = (checksum % 0xFF );
            ret = map->readFunc(map->group_info[i].group_addr_info[j].group_checksum_page,map->group_info[i].group_addr_info[j].group_checksum_addr,&readByte);
            if(checksum == readByte){
               LOG_INFO("333 groupinfo: %s, checksum OK c(%04d) r(%04d)",map->group_info[i].group_name,checksum,readByte);
            }else{
               LOG_ERR("333 groupinfo: %s, checksum ERROR ret=%d, checksum=%04d readByte=%04d",map->group_info[i].group_name,ret,checksum,readByte);
                ret = -ERROR_CHECKSUM;
                }
       }else if(map->group_checksum == 1){
            checksum = checksum % 0xFFFF;
            readByte_checksum = 0;
            ret = map->readFunc(map->group_info[i].group_addr_info[j].group_checksum_page,
                                map->group_info[i].group_addr_info[j].group_checksum_addr,
                                    &readByte2);
            ret = map->readFunc(map->group_info[i].group_addr_info[j].group_checksum_page,
                                map->group_info[i].group_addr_info[j].group_checksum_addr+1,
                                    &readByte);
            readByte_checksum = (readByte2 << 8) | readByte;

            if(checksum == readByte_checksum){
                LOG_INFO("groupinfo: %s, checksum OK c(%04d) r(%04d)",map->group_info[i].group_name,checksum,readByte_checksum);
            }else{
                LOG_ERR("groupinfo: %s, checksum ERROR ret=%d, checksum=%04d readByte=%04d",
                                map->group_info[i].group_name,ret,checksum,readByte_checksum);
                ret = -ERROR_CHECKSUM;
                }

        }
        LOG_INFO("groupinfo: %s,end_addr 0x%04x(%04d) size 0x%04x(%04d)",
                        map->group_info[i].group_name,curr_addr-1,curr_addr-1,size,size);

    }

    return ret;
}


int hi556_read_data(u8 page,u16 addr,u8 *data)
{
    static u16 last_addr = 0;
    if( addr != last_addr+ 1){
        write_reg16_data8(0x010a,(addr >>8) & 0xff);
        write_reg16_data8(0x010b, addr & 0xff);
        write_reg16_data8(0x0102,0x01);
    }

    last_addr = addr;
    return read_reg16_data8(0x0108,data);
}

void hi556_otp_read_enable()
{
    write_reg16_data16(0x0e00,0x0102);
    write_reg16_data16(0x0e02,0x0102);
    write_reg16_data16(0x0e0c,0x0100);
    write_reg16_data16(0x27fe,0xe000);
    write_reg16_data16(0x0b0e,0x8600);
    write_reg16_data16(0x0d04,0x0100);
    write_reg16_data16(0x0d02,0x0707);
    write_reg16_data16(0x0f30,0x6e25);

    write_reg16_data16(0x0f32,0x7067);
    write_reg16_data16(0x0f02,0x0106);
    write_reg16_data16(0x0a04,0x0000);
    write_reg16_data16(0x0e0a,0x0001);
    write_reg16_data16(0x004a,0x0100);
    write_reg16_data16(0x003e,0x1000);
    write_reg16_data16(0x0a00,0x0100);

    write_reg16_data8(0x0a02,0x01);
    write_reg16_data8(0x0a00,0x00);
    msleep(10);
    write_reg16_data8(0x0f02,0x00);
    write_reg16_data8(0x011a,0x01);
    write_reg16_data8(0x011b,0x09);
    write_reg16_data8(0x0d04,0x01);
    write_reg16_data8(0x0d02,0x07);
    write_reg16_data8(0x003e,0x10);
    write_reg16_data8(0x0a00,0x01);
}

void hi556_otp_read_disable()
{
    write_reg16_data8(0x0a00,0x00);
    msleep(10);
    write_reg16_data8(0x004a,0x00);
    write_reg16_data8(0x0d04,0x00);
    write_reg16_data8(0x003e,0x00);
    write_reg16_data8(0x004a,0x01);
    write_reg16_data8(0x0a00,0x01);
}



unsigned int Hi556_read_region(struct i2c_client *client, unsigned int addr,unsigned char *data,unsigned int size)
{
    int ret = 0;
    if(g_otp_buf[IMGSENSOR_SENSOR_IDX_SUB][READ_FLAG_ADDR] ){
        LOG_INFO("from mem addr 0x%x,size %d",addr,size);
        memcpy((void *)data,&g_otp_buf[IMGSENSOR_SENSOR_IDX_SUB][addr],size);
        return size;
    }
    if(client != NULL){

        g_pstI2CclientG = client;

    }else if(g_pstI2Cclients[IMGSENSOR_SENSOR_IDX_SUB] != NULL){

        g_pstI2CclientG = g_pstI2Cclients[IMGSENSOR_SENSOR_IDX_SUB];
        g_pstI2CclientG->addr = 0x40 >> 1;
    }


    hi556_otp_read_enable();
    ret = parse_otp_map_data(&hi556_otp_map,&g_otp_buf[IMGSENSOR_SENSOR_IDX_SUB][0]);
    if(!ret){
       g_otp_buf[IMGSENSOR_SENSOR_IDX_SUB][CHECKSUM_FLAG_ADDR] = OTP_DATA_GOOD_FLAG;
       g_otp_buf[IMGSENSOR_SENSOR_IDX_SUB][READ_FLAG_ADDR] = 1;
    }
    hi556_otp_read_disable();
    if(NULL != data){
       memcpy((void *)data,&g_otp_buf[IMGSENSOR_SENSOR_IDX_SUB][addr],size);
    }
    return ret;
}

int s5k3h7yx_read_data(u8 page,u16 addr,u8 *data)
{
    u8 get_byte = 0;
    int ret = 0;

    write_reg16_data8(0x0A02,page);
    write_reg16_data8(0x0A00,0x01);

    do{
        mdelay(1);
        ret = read_reg16_data8(0x0A01,&get_byte);
    }while((get_byte & 0x01) != 1);

    ret = read_reg16_data8(addr,data);
    write_reg16_data8(0x0A00,0x00);
    return ret;
}

unsigned int s5k3h7yx_read_region(struct i2c_client *client, unsigned int addr,unsigned char *data,unsigned int size)
{
    int ret = 0;
    if(g_otp_buf[IMGSENSOR_SENSOR_IDX_MAIN][READ_FLAG_ADDR] ){
        LOG_INFO("from mem addr 0x%x,size %d",addr,size);
        memcpy((void *)data,&g_otp_buf[IMGSENSOR_SENSOR_IDX_MAIN][addr],size);
        return size;
    }
    if(client != NULL){

        g_pstI2CclientG = client;

    }else if(g_pstI2Cclients[IMGSENSOR_SENSOR_IDX_MAIN] != NULL){

        g_pstI2CclientG = g_pstI2Cclients[IMGSENSOR_SENSOR_IDX_MAIN];

        g_pstI2CclientG->addr = 0x20 >> 1;
    }
    ret = parse_otp_map_data(&s5k3h7yx_otp_map,&g_otp_buf[IMGSENSOR_SENSOR_IDX_MAIN][0]);
    if(!ret){
        g_otp_buf[IMGSENSOR_SENSOR_IDX_MAIN][CHECKSUM_FLAG_ADDR] = OTP_DATA_GOOD_FLAG;
        g_otp_buf[IMGSENSOR_SENSOR_IDX_MAIN][READ_FLAG_ADDR] = 1;
    }
    if(NULL != data){
        memcpy((void *)data,&g_otp_buf[IMGSENSOR_SENSOR_IDX_MAIN][addr],size);
    }
    return ret;
}

int gc5035_read_data(u8 page,u16 addr,u8 *data)
{
    int ret = 0;
    ret = read_reg16_data8(addr,data);
    return ret;
}

unsigned int gc5035_read_region(struct i2c_client *client, unsigned int addr,unsigned char *data,unsigned int size)
{
    int ret = 0;
    if(g_otp_buf[IMGSENSOR_SENSOR_IDX_SUB][READ_FLAG_ADDR] ){
        LOG_INFO("from mem addr 0x%x,size %d",addr,size);
        memcpy((void *)data,&g_otp_buf[IMGSENSOR_SENSOR_IDX_SUB][addr],size);
        return size;
    }
    if(client != NULL){

        g_pstI2CclientG = client;

    }else if(g_pstI2Cclients[IMGSENSOR_SENSOR_IDX_SUB] != NULL){

        g_pstI2CclientG = g_pstI2Cclients[IMGSENSOR_SENSOR_IDX_SUB];
        g_pstI2CclientG->addr = 0xA4 >> 1;
    }
    ret = parse_otp_map_data(&gc5035_otp_map,&g_otp_buf[IMGSENSOR_SENSOR_IDX_SUB][0]);
    if(!ret){
        g_otp_buf[IMGSENSOR_SENSOR_IDX_SUB][CHECKSUM_FLAG_ADDR] = OTP_DATA_GOOD_FLAG;
        g_otp_buf[IMGSENSOR_SENSOR_IDX_SUB][READ_FLAG_ADDR] = 1;
    }
    if(NULL != data){
        memcpy((void *)data,&g_otp_buf[IMGSENSOR_SENSOR_IDX_SUB][addr],size);
    }
    return ret;
}

unsigned int s5k3h7yx_txd_read_region(struct i2c_client *client, unsigned int addr,unsigned char *data,unsigned int size)
{
    int ret = 0;
    if(g_otp_buf[IMGSENSOR_SENSOR_IDX_MAIN][READ_FLAG_ADDR] ){
        LOG_INFO("from mem addr 0x%x,size %d",addr,size);
        memcpy((void *)data,&g_otp_buf[IMGSENSOR_SENSOR_IDX_MAIN][addr],size);
        return size;
    }
    if(client != NULL){

        g_pstI2CclientG = client;

    }else if(g_pstI2Cclients[IMGSENSOR_SENSOR_IDX_MAIN] != NULL){

        g_pstI2CclientG = g_pstI2Cclients[IMGSENSOR_SENSOR_IDX_MAIN];
        g_pstI2CclientG->addr = 0x20 >> 1;
    }
    ret = parse_otp_map_data(&s5k3h7yx_txd_otp_map,&g_otp_buf[IMGSENSOR_SENSOR_IDX_MAIN][0]);
    if(!ret){
        g_otp_buf[IMGSENSOR_SENSOR_IDX_MAIN][CHECKSUM_FLAG_ADDR] = OTP_DATA_GOOD_FLAG;
        g_otp_buf[IMGSENSOR_SENSOR_IDX_MAIN][READ_FLAG_ADDR] = 1;
    }
    if(NULL != data){
        memcpy((void *)data,&g_otp_buf[IMGSENSOR_SENSOR_IDX_MAIN][addr],size);
    }

    return ret;
}
unsigned int gc5035_ofg_read_region(struct i2c_client *client, unsigned int addr,unsigned char *data,unsigned int size)
{
    int ret = 0;
    if(g_otp_buf[IMGSENSOR_SENSOR_IDX_SUB][READ_FLAG_ADDR] ){
        LOG_INFO("from mem addr 0x%x,size %d",addr,size);
        memcpy((void *)data,&g_otp_buf[IMGSENSOR_SENSOR_IDX_SUB][addr],size);
        return size;
    }
    LOG_INFO("gc5035_ofg_read_region start:");
    if(client != NULL){
        g_pstI2CclientG = client;
    }else if(g_pstI2Cclients[IMGSENSOR_SENSOR_IDX_SUB] != NULL){
        g_pstI2CclientG = g_pstI2Cclients[IMGSENSOR_SENSOR_IDX_SUB];
        g_pstI2CclientG->addr = 0xA8 >> 1;
    }
    ret = parse_otp_map_data(&gc5035_ofg_otp_map,&g_otp_buf[IMGSENSOR_SENSOR_IDX_SUB][0]);
    if(!ret){
        g_otp_buf[IMGSENSOR_SENSOR_IDX_SUB][CHECKSUM_FLAG_ADDR] = OTP_DATA_GOOD_FLAG;
        g_otp_buf[IMGSENSOR_SENSOR_IDX_SUB][READ_FLAG_ADDR] = 1;
    }
    if(NULL != data){
        memcpy((void *)data,&g_otp_buf[IMGSENSOR_SENSOR_IDX_SUB][addr],size);
    }
    return ret;
}
unsigned int s5k3h7yx_ts_read_region(struct i2c_client *client, unsigned int addr,unsigned char *data,unsigned int size)
{
    int ret = 0;
    if(g_otp_buf[IMGSENSOR_SENSOR_IDX_MAIN][READ_FLAG_ADDR] ){
        LOG_INFO("from mem addr 0x%x,size %d",addr,size);
        memcpy((void *)data,&g_otp_buf[IMGSENSOR_SENSOR_IDX_MAIN][addr],size);
        return size;
    }
    if(client != NULL){
        g_pstI2CclientG = client;
    }else if(g_pstI2Cclients[IMGSENSOR_SENSOR_IDX_MAIN] != NULL){
        g_pstI2CclientG = g_pstI2Cclients[IMGSENSOR_SENSOR_IDX_MAIN];
        g_pstI2CclientG->addr = 0x20 >> 1;
    }
    ret = parse_otp_map_data(&s5k3h7yx_ts_otp_map,&g_otp_buf[IMGSENSOR_SENSOR_IDX_MAIN][0]);
    if(!ret){
        g_otp_buf[IMGSENSOR_SENSOR_IDX_MAIN][CHECKSUM_FLAG_ADDR] = OTP_DATA_GOOD_FLAG;
        g_otp_buf[IMGSENSOR_SENSOR_IDX_MAIN][READ_FLAG_ADDR] = 1;
    }
    if(NULL != data){
        memcpy((void *)data,&g_otp_buf[IMGSENSOR_SENSOR_IDX_MAIN][addr],size);
    }
    return ret;
}
