#pragma once

#ifndef BASISU_SHARED
    #define BASISU_API
#elif defined(_WIN32)
    #ifdef basisu_encoder_EXPORTS
        #define BASISU_API __declspec(dllexport)
    #else
        #define BASISU_API __declspec(dllimport)
    #endif
#else
    #ifdef basisu_encoder_EXPORTS
        #define BASISU_API __attribute__((visibility("default")))
    #else
        #define BASISU_API
    #endif
#endif