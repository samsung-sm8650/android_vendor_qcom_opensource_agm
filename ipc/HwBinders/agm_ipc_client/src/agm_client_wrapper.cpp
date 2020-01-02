/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#define LOG_TAG "agm_client_wrapper"

#include <vendor/qti/hardware/AGMIPC/1.0/IAGMCallback.h>
#include <hidl/LegacySupport.h>
#include <log/log.h>
#include <unistd.h>
#include <vendor/qti/hardware/AGMIPC/1.0/IAGM.h>

#include "agm_api.h"
#include "inc/AGMCallback.h"

using android::hardware::Return;
using android::hardware::hidl_vec;
using vendor::qti::hardware::AGMIPC::V1_0::IAGM;
using vendor::qti::hardware::AGMIPC::V1_0::IAGMCallback;
using vendor::qti::hardware::AGMIPC::V1_0::implementation::AGMCallback;
using vendor::qti::hardware::AGMIPC::V1_0::MmapBufInfo;
using android::hardware::defaultPassthroughServiceImplementation;
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::sp;

bool agm_server_died = false;
static pthread_mutex_t agmclient_init_lock = PTHREAD_MUTEX_INITIALIZER;
android::sp<IAGM> agm_client = NULL;
sp<server_death_notifier> Server_death_notifier = NULL;

void server_death_notifier::serviceDied(uint64_t cookie,
                   const android::wp<::android::hidl::base::V1_0::IBase>& who __unused)
{
    ALOGE("%s : AGM Service died ,cookie : %llu",__func__, (unsigned long long)cookie);
    agm_server_died = true;
    if (cb_ != NULL)
        cb_(cookie_);
    // We exit the client process here, so that it also can restart
    // leading to a fresh start on both the sides.
}

android::sp<IAGM> get_agm_server() {
    pthread_mutex_lock(&agmclient_init_lock);
    if (agm_client == NULL) {
        agm_client = IAGM::getService();
        if (agm_client == nullptr) {
            ALOGE("AGM service not initialized\n");
            goto done;
        }
        if(Server_death_notifier == NULL)
        {
            Server_death_notifier = new server_death_notifier();
            agm_client->linkToDeath(Server_death_notifier,0);
            ALOGI("%s : server linked to death \n", __func__);
        }
    }
done:
    pthread_mutex_unlock(&agmclient_init_lock);
    return agm_client ;
}

int agm_register_service_crash_callback(agm_service_crash_cb cb, uint64_t cookie)
{
    int ret = 0;
    pthread_mutex_lock(&agmclient_init_lock);
    if (agm_client == NULL) {
        agm_client = IAGM::getService();
        if (agm_client == nullptr) {
            ALOGE("AGM service not initialized\n");
            ret = -ESRCH;
            goto done;
        }
    }
    if (Server_death_notifier == NULL) {
        Server_death_notifier = new server_death_notifier(cb, cookie);
        agm_client->linkToDeath(Server_death_notifier,0);
        ALOGI("%s : server linked to death \n", __func__);
    } else {
        Server_death_notifier->register_crash_cb(cb, cookie);
    }
done:
    pthread_mutex_unlock(&agmclient_init_lock);
    return 0;
}

int agm_aif_set_media_config(uint32_t audio_intf,
                                struct agm_media_config *media_config) {
    ALOGV("%s called audio_intf = %d \n", __func__, audio_intf);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        hidl_vec<AgmMediaConfig> media_config_hidl;
        media_config_hidl.resize(sizeof(struct agm_media_config));
        media_config_hidl.data()->rate = media_config->rate;
        media_config_hidl.data()->channels = media_config->channels;
        media_config_hidl.data()->format = (::vendor::qti::hardware::AGMIPC::V1_0::AgmMediaFormat) media_config->format;
        media_config_hidl.data()->data_format = media_config->data_format;
        return agm_client->ipc_agm_aif_set_media_config(audio_intf,
                                                        media_config_hidl);
    }
    return -EINVAL;
}

int agm_session_set_config(uint64_t handle,
                            struct agm_session_config *session_config,
                            struct agm_media_config *media_config,
                            struct agm_buffer_config *buffer_config) {
    ALOGE("%s called with handle = %llx \n", __func__, (unsigned long long)handle);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        hidl_vec<AgmSessionConfig> session_config_hidl;
        session_config_hidl.resize(sizeof(struct agm_session_config));
        memcpy(session_config_hidl.data(),
               session_config,
               sizeof(struct agm_session_config));

        hidl_vec<AgmMediaConfig> media_config_hidl;
        media_config_hidl.resize(sizeof(struct agm_media_config));
        media_config_hidl.data()->rate = media_config->rate;
        media_config_hidl.data()->channels = media_config->channels;
        media_config_hidl.data()->format = (::vendor::qti::hardware::AGMIPC::V1_0::AgmMediaFormat) media_config->format;
        media_config_hidl.data()->data_format = media_config->data_format;

        hidl_vec<AgmBufferConfig> buffer_config_hidl;
        buffer_config_hidl.resize(sizeof(struct agm_buffer_config));
        buffer_config_hidl.data()->count = buffer_config->count;
        buffer_config_hidl.data()->size = buffer_config->size;
        ALOGV("%s : Exit", __func__);
        return agm_client->ipc_agm_session_set_config(handle,
                                                      session_config_hidl,
                                                      media_config_hidl,
                                                      buffer_config_hidl);
    }
    return -EINVAL;
}


int agm_init(){
     /*agm_init in IPC happens in context of the server*/
      return 0;
}

int agm_deinit(){
     /*agm_deinit in IPC happens in context of the server*/
      return 0;
}

int agm_aif_set_metadata(uint32_t audio_intf, uint32_t size, uint8_t *metadata){
    ALOGV("%s called aif = %d, size =%d \n", __func__, audio_intf, size);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        hidl_vec<uint8_t> metadata_hidl;
        metadata_hidl.resize(size);
        memcpy(metadata_hidl.data(), metadata, size);
        int32_t ret = agm_client->ipc_agm_aif_set_metadata(audio_intf,
                                                           size, metadata_hidl);
        return ret;
    }
    return -EINVAL;
}

int agm_session_set_metadata(uint32_t session_id,
                             uint32_t size,
                             uint8_t *metadata){
    ALOGV("%s called sess_id = %d, size = %d\n", __func__, session_id, size);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        hidl_vec<uint8_t> metadata_hidl;
        metadata_hidl.resize(size);
        memcpy(metadata_hidl.data(), metadata, size);
        int32_t ret = agm_client->ipc_agm_session_set_metadata(session_id,
                                                               size,
                                                               metadata_hidl);
        return ret;
    }
    return -EINVAL;
}

int agm_session_aif_set_metadata(uint32_t session_id, uint32_t audio_intf,
                                 uint32_t size, uint8_t *metadata){
    ALOGV("%s called with sess_id = %d, aif = %d, size = %d\n", __func__,
                                                 session_id, audio_intf, size);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        hidl_vec<uint8_t> metadata_hidl;
        metadata_hidl.resize(size);
        memcpy(metadata_hidl.data(), metadata, size);
        int32_t ret = agm_client->ipc_agm_session_aif_set_metadata(session_id,
                                                                audio_intf,
                                                                size,
                                                                metadata_hidl);
        return ret;
    }
    return -EINVAL;
}

int agm_session_close(uint64_t handle){
    ALOGV("%s called with handle = %llx \n", __func__, (unsigned long long) handle);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        return agm_client->ipc_agm_session_close(handle);
    }
    return -EINVAL;
}

int agm_session_prepare(uint64_t handle){
    ALOGV("%s called with handle = %llx \n", __func__, (unsigned long long) handle);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        return agm_client->ipc_agm_session_prepare(handle);
    }
    return -EINVAL;
}

int agm_session_start(uint64_t handle){
    ALOGV("%s called with handle = %llx \n", __func__, (unsigned long long) handle);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        return agm_client->ipc_agm_session_start(handle);
    }
    return -EINVAL;
}

int agm_session_stop(uint64_t handle){
    ALOGV("%s called with handle = %llx \n", __func__, (unsigned long long) handle);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        return agm_client->ipc_agm_session_stop(handle);
    }
    return -EINVAL;
}

int agm_session_pause(uint64_t handle){
    ALOGV("%s called with handle = %llx \n", __func__, (unsigned long long) handle);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        return agm_client->ipc_agm_session_pause(handle);
    }
    return -EINVAL;
}

int agm_session_resume(uint64_t handle){
    ALOGV("%s called with handle = %llx \n", __func__, (unsigned long long) handle);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        return agm_client->ipc_agm_session_resume(handle);
    }
    return -EINVAL;
}

int agm_session_open(uint32_t session_id, enum agm_session_mode sess_mode ,
                     uint64_t *handle) {
    ALOGE("%s called with handle = %x , *handle = %x\n", __func__, handle, *handle);
    int ret = -EINVAL;
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        AgmSessionMode sess_mode_hidl = (AgmSessionMode) sess_mode;
        agm_client->ipc_agm_session_open(session_id, sess_mode_hidl,
                              [&](int32_t _ret, hidl_vec<uint64_t> handle_hidl)
                              {  ret = _ret;
                                 *handle = *handle_hidl.data();
                              });
    }
    ALOGE("%s Received handle = %p , *handle = %llx\n", __func__, handle, (unsigned long long) *handle);
    return ret;
}

int  agm_session_aif_connect(uint32_t session_id,
                             uint32_t audio_intf,
                             bool state) {
    ALOGV("%s called with sess_id = %d, aif = %d, state = %s\n", __func__,
           session_id, audio_intf, state ? "true" : "false" );
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        return agm_client->ipc_agm_session_aif_connect(session_id,
                                                       audio_intf,
                                                       state);
    }
    return -EINVAL;
}

int agm_session_read(uint64_t handle, void *buf, size_t *byte_count){
    ALOGV("%s called with handle = %llx \n", __func__, (unsigned long long) handle);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        if (!handle)
            return -EINVAL;

        int ret = -EINVAL;

        agm_client->ipc_agm_session_read(handle, *byte_count,
                   [&](int32_t _ret, hidl_vec<uint8_t> buff_hidl, uint32_t cnt)
                   { ret = _ret;
                     if (ret != -ENOMEM) {
                         memcpy(buf, buff_hidl.data(), *byte_count);
                         *byte_count = (size_t) cnt;
                     }
                   });
        return ret;
    }
    return -EINVAL;
}

int agm_session_write(uint64_t handle, void *buf, size_t *byte_count) {
    ALOGV("%s called with handle = %llx \n", __func__, (unsigned long long) handle);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        int ret = -EINVAL;

        if (!handle)
            return -EINVAL;

        hidl_vec<uint8_t> buf_hidl;
        buf_hidl.resize(*byte_count);
        memcpy(buf_hidl.data(), buf, *byte_count);

        agm_client->ipc_agm_session_write(handle, buf_hidl, *byte_count,
                                           [&](int32_t _ret, uint32_t cnt)
                                           { ret = _ret;
                                             if (ret != -ENOMEM)
                                                 *byte_count = (size_t) cnt;
                                           });
        return ret;
    }
    return -EINVAL;
}


int agm_session_set_loopback(uint32_t capture_session_id,
                             uint32_t playback_session_id,
                             bool state)
{
    ALOGV("%s called capture_session_id = %d, playback_session_id = %d\n", __func__,
           capture_session_id, playback_session_id);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        return agm_client->ipc_agm_session_set_loopback(capture_session_id,
                                                        playback_session_id,
                                                        state);
    }
    return -EINVAL;
}

size_t agm_get_hw_processed_buff_cnt(uint64_t handle, enum direction dir) {
    ALOGV("%s called with handle = %llx \n", __func__, (unsigned long long) handle);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        Direction dir_hidl = (Direction) dir;
        return agm_client->ipc_agm_get_hw_processed_buff_cnt(handle,
                                                             dir_hidl);
    }
    return -EINVAL;
}

int agm_get_aif_info_list(struct aif_info *aif_list, size_t *num_aif_info) {
    ALOGV("%s called \n", __func__);
    if (!agm_server_died) {
        uint32_t num = (uint32_t) *num_aif_info;
        int ret = -EINVAL;
        android::sp<IAGM> agm_client = get_agm_server();
        agm_client->ipc_agm_get_aif_info_list(num,[&](int32_t _ret,
                                            hidl_vec<AifInfo> aif_list_ret_hidl,
                                            uint32_t num_aif_info_hidl )
        { ret = _ret;
          if (ret != -ENOMEM) {
              if (aif_list != NULL) {
                  for (int i=0 ; i<num_aif_info_hidl ; i++) {
                      strlcpy(aif_list[i].aif_name,
                              aif_list_ret_hidl.data()[i].aif_name.c_str(),
                              AIF_NAME_MAX_LEN);
                      ALOGV("%s : The retrived %d aif_name = %s \n", __func__, i,
                                                              aif_list[i].aif_name);
                      aif_list[i].dir = (enum direction)
                                                    aif_list_ret_hidl.data()[i].dir;
                  }
              }
          *num_aif_info = (size_t) num_aif_info_hidl;
          }
        });
        return ret;
    }
    return -EINVAL;
}

int agm_session_aif_get_tag_module_info(uint32_t session_id, uint32_t aif_id,
                                        void *payload, size_t *size)
{
    ALOGV("%s : sess_id = %d, aif_id = %d\n", __func__, session_id, aif_id);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        uint32_t size_hidl = (uint32_t) *size;
        int ret = 0;
        agm_client->ipc_agm_session_aif_get_tag_module_info(
                            session_id,
                            aif_id,
                            size_hidl,
                            [&](int32_t _ret,
                                hidl_vec<uint8_t> payload_ret,
                                uint32_t size_ret)
                            { ret = _ret;
                              if (ret != -ENOMEM) {
                                  if (payload != NULL)
                                      memcpy(payload, payload_ret.data(), size_ret);
                                  else if (size_ret == 0)
                                      ALOGE("%s : received NULL Payload",__func__);
                                  *size = (size_t) size_ret;
                              }
                            });
        return ret;
    }
    return -EINVAL;
}

int agm_session_get_params(uint32_t session_id, void *payload, size_t size)
{
    ALOGV("%s : sess_id = %d, size = %zu\n", __func__, session_id, size);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        hidl_vec<uint8_t> buf_hidl;
        int ret = 0;

        buf_hidl.resize(size);
        memcpy(buf_hidl.data(), payload, size);
        agm_client->ipc_agm_session_get_params(session_id, size, buf_hidl,
                           [&](int32_t _ret, hidl_vec<uint8_t> payload_ret)
                           { ret = _ret;
                             if (!ret) {
                                 if (payload != NULL)
                                     memcpy(payload, payload_ret.data(), size);
                             }
                           });
        return ret;
    }
    return -EINVAL;
}

int agm_aif_set_params(uint32_t aif_id, void *payload, size_t size)
{
    ALOGV("%s : aif_id = %d\n", __func__, aif_id);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();

        uint32_t size_hidl = (uint32_t) size;
        hidl_vec<uint8_t> payload_hidl;
        payload_hidl.resize(size_hidl);
        memcpy(payload_hidl.data(), payload, size_hidl);

        return agm_client->ipc_agm_aif_set_params(aif_id,
                                            payload_hidl, size_hidl);
    }
    return -EINVAL;
}

int agm_session_aif_set_params(uint32_t session_id, uint32_t aif_id,
                                                    void *payload, size_t size)
{
    ALOGV("%s : sess_id = %d, aif_id = %d\n", __func__, session_id, aif_id);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();

        uint32_t size_hidl = (uint32_t) size;
        hidl_vec<uint8_t> payload_hidl;
        payload_hidl.resize(size_hidl);
        memcpy(payload_hidl.data(), payload, size_hidl);

        return agm_client->ipc_agm_session_aif_set_params(session_id,
                                                          aif_id,
                                                          payload_hidl,
                                                          size_hidl);
    }
    return -EINVAL;
}

int agm_session_set_params(uint32_t session_id, void *payload, size_t size)
{
    ALOGV("%s : sess_id = %d, size = %zu\n", __func__, session_id, size);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();

        uint32_t size_hidl = (uint32_t) size;
        hidl_vec<uint8_t> payload_hidl;
        payload_hidl.resize(size_hidl);
        memcpy(payload_hidl.data(), payload, size_hidl);
        return agm_client->ipc_agm_session_set_params(session_id,
                                                      payload_hidl,
                                                      size_hidl);
    }
    return -EINVAL;
}

int agm_set_params_with_tag(uint32_t session_id, uint32_t aif_id,
                                             struct agm_tag_config *tag_config)
{
    ALOGV("%s : sess_id = %d, aif_id = %d\n", __func__, session_id, aif_id);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();

        hidl_vec<AgmTagConfig> tag_cfg_hidl;
        tag_cfg_hidl.resize(sizeof(struct agm_tag_config) +
                             (tag_config->num_tkvs)*sizeof(agm_key_value));
        tag_cfg_hidl.data()->tag = tag_config->tag;
        tag_cfg_hidl.data()->num_tkvs = tag_config->num_tkvs;
        AgmKeyValue * ptr = NULL;
        for (int i=0 ; i < tag_cfg_hidl.data()->num_tkvs ; i++ ) {
             ptr = (AgmKeyValue *)(tag_cfg_hidl.data()
                                  + sizeof(struct agm_tag_config)
                                  + (sizeof(AgmKeyValue) * i));
             ptr->key = tag_config->kv[i].key;
             ptr->value = tag_config->kv[i].value;
    }
        return agm_client->ipc_agm_set_params_with_tag(session_id,
                                                         aif_id, tag_cfg_hidl);
    }
    return -EINVAL;
}

int agm_session_register_for_events(uint32_t session_id,
                                          struct agm_event_reg_cfg *evt_reg_cfg)
{
    ALOGV("%s : sess_id = %d\n", __func__, session_id);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();

        hidl_vec<AgmEventRegCfg> evt_reg_cfg_hidl;
        size_t size_local = sizeof(struct agm_event_reg_cfg) +
                      (evt_reg_cfg->event_config_payload_size);
        evt_reg_cfg_hidl.resize(size_local);

        evt_reg_cfg_hidl.data()->module_instance_id = evt_reg_cfg->module_instance_id;
        evt_reg_cfg_hidl.data()->event_id = evt_reg_cfg->event_id;
        evt_reg_cfg_hidl.data()->event_config_payload_size = evt_reg_cfg->event_config_payload_size;
        evt_reg_cfg_hidl.data()->is_register = evt_reg_cfg->is_register;
        for (int i = 0; i < evt_reg_cfg->event_config_payload_size; i++)
            evt_reg_cfg_hidl.data()->event_config_payload[i] = evt_reg_cfg->event_config_payload[i];

        return agm_client->ipc_agm_session_register_for_events(session_id,
                                                              evt_reg_cfg_hidl);
    }
    return -EINVAL;
}

int agm_session_aif_set_cal(uint32_t session_id ,uint32_t aif_id ,
                                              struct agm_cal_config *cal_config)
{
    ALOGV("%s : sess_id = %d, aif_id = %d\n", __func__, session_id, aif_id);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();

        hidl_vec<AgmCalConfig> cal_cfg_hidl;
        cal_cfg_hidl.resize(sizeof(AgmCalConfig) +
                            (cal_config->num_ckvs)*sizeof(AgmKeyValue));
        cal_cfg_hidl.data()->num_ckvs = cal_config->num_ckvs;
        AgmKeyValue * ptr = NULL;
        for (int i=0 ; i < cal_cfg_hidl.data()->num_ckvs ; i++ ) {
            ptr = (AgmKeyValue *) (cal_cfg_hidl.data() +
                                   sizeof(struct agm_cal_config) +
                                   (sizeof(AgmKeyValue)*i));
            ptr->key = cal_config->kv[i].key;
            ptr->value = cal_config->kv[i].value;
        }
        return agm_client->ipc_agm_session_aif_set_cal(session_id, aif_id,
                                                                  cal_cfg_hidl);
    }
    return -EINVAL;
}

int agm_session_set_ec_ref(uint32_t capture_session_id,
                           uint32_t aif_id, bool state)
{
    ALOGV("%s : cap_sess_id = %d, aif_id = %d\n", __func__,
                                  capture_session_id, aif_id);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        return agm_client->ipc_agm_session_set_ec_ref(capture_session_id,
                                                       aif_id, state);
    }
    return -EINVAL;
}

int agm_session_register_cb(uint32_t session_id, agm_event_cb cb,
                             enum event_type evt_type, void *client_data)
{
    ALOGV("%s : sess_id = %d, evt_type = %d, client_data = %p \n", __func__,
           session_id, evt_type, client_data);
    if (!agm_server_died) {
        sp<IAGMCallback> ClbkBinder = NULL;
        ClntClbk *cl_clbk_data = NULL;
        android::sp<IAGM> agm_client = get_agm_server();
        if (cb != NULL) {
            cl_clbk_data = new ClntClbk(session_id, cb, evt_type, client_data);
            ClbkBinder = new AGMCallback();
        }
        uint64_t cl_clbk_data_add = (uint64_t) cl_clbk_data;
        int ret = agm_client->ipc_agm_session_register_callback(
                                                        session_id,
                                                        ClbkBinder,
                                                        evt_type,
                                                        cl_clbk_data_add,
                                                        (uint64_t )client_data);
        return ret;
    }
    return -EINVAL;
}

int agm_session_eos(uint64_t handle)
{
    ALOGV("%s called with handle = %llx \n", __func__, (unsigned long long) handle);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        return agm_client->ipc_agm_session_eos(handle);
    }
    return -EINVAL;
}

int agm_get_session_time(uint64_t handle, uint64_t *timestamp)
{
    ALOGV("%s called with handle = %llx \n", __func__, (unsigned long long) handle);
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        int ret = -EINVAL;
        agm_client->ipc_agm_get_session_time(handle,
                                             [&](int _ret, uint64_t ts)
                                             { ret = _ret;
                                               *timestamp = ts;
                                             });
    }
    return -EINVAL;
}

int agm_get_buffer_timestamp(uint32_t session_id, uint64_t *timestamp)
{
    ALOGV("%s: session_id = %x\n", __func__, session_id);
    int ret = -EINVAL;
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        agm_client->ipc_agm_get_buffer_timestamp(session_id,
                                             [&](int _ret, uint64_t ts)
                                             { ret = _ret;
                                               *timestamp = ts;
                                             });
    }
    return ret;
}

int agm_session_get_buf_info(uint32_t session_id, struct agm_buf_info *buf_info, uint32_t flag)
{
    ALOGV("%s : session_id = %d\n", __func__, session_id);
    int ret = -EINVAL;
    if (!agm_server_died) {
        android::sp<IAGM> agm_client = get_agm_server();
        const native_handle *datahandle = nullptr;
        const native_handle *poshandle = nullptr;

        agm_client->ipc_agm_session_get_buf_info(session_id, flag,
                [&](int32_t _ret, const MmapBufInfo& buf_info_ret_hidl)
                { ret = _ret;
                if (!ret) {
                if (flag & DATA_BUF) {
                datahandle = buf_info_ret_hidl.dataSharedMemory.handle();
                buf_info->data_buf_fd = dup(datahandle->data[0]);
                buf_info->data_buf_size = buf_info_ret_hidl.data_size;
                }
                if (flag & POS_BUF) {
                poshandle = buf_info_ret_hidl.posSharedMemory.handle();
                buf_info->pos_buf_fd = poshandle->data[0];
                buf_info->pos_buf_size = buf_info_ret_hidl.pos_size;
                }
                }
                });
    }
    return ret;
}

