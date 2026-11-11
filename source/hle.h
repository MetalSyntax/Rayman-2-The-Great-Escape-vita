/**
 * @file hle.h
 * @brief Canonical storage path constants for PS Vita high-level emulation.
 */
#pragma once

#ifndef DATA_PATH
#define DATA_PATH "ux0:data/rayman2thegreatescape/"
#endif

#define BUNDLE_PATH DATA_PATH "game_app/"
#define DOCS_PATH   DATA_PATH "saves/"
#define LOGS_PATH   DATA_PATH "logs/"
#define LOG_PREFIX  "rayman2thegreatescape_"

// Identidad del bundle iOS (game_app/Info.plist)
#define EXE_NAME     "Rayman2"
#define EXE_PATH     BUNDLE_PATH EXE_NAME
#define BUNDLE_ID    "com.gameloft.Rayman2"
#define BUNDLE_VER   "1.0.1"
#define APP_DELEGATE "Rayman2AppDelegate"
