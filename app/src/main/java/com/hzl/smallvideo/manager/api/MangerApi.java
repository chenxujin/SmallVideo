package com.hzl.smallvideo.manager.api;

/**
 * 作者：请叫我百米冲刺 on 2017/11/7 上午10:56
 * 邮箱：mail@hezhilin.cc
 */

public interface MangerApi {

    void onResume();

    void onStop();

    void onDestroy();

    void startRecord();

    void stopRecord();
}
