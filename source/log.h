/**
 * @file log.h
 * @brief Thread-safe logging subsystem with rotating disk logs and stdout mirrors.
 */
#pragma once

/**
 * @brief Opens target log file on disk.
 * @param[in] path Filesystem destination path.
 */
void log_open(const char *path);

/** @brief 0 (boot default): every line written immediately; 1: INFO lines buffered (after first frame). */
void log_set_buffered(int on);

/** @brief Writes the buffered lines without locking (crash handlers only). */
void log_flush_unlocked(void);

/**
 * @brief Logs a verbose debug message (compiled out unless DEBUG_LOADER is defined).
 */
void l_debug(const char *fmt, ...);

/**
 * @brief Logs an informational message.
 */
void l_info(const char *fmt, ...);

/**
 * @brief Logs a warning message.
 */
void l_warn(const char *fmt, ...);

/**
 * @brief Logs an error message.
 */
void l_error(const char *fmt, ...);

/**
 * @brief Logs a fatal error message and terminates process execution.
 */
void fatal_error(const char *fmt, ...) __attribute__((noreturn));
