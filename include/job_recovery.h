/**
 * @file job_recovery.h
 * @brief Power Loss Recovery - Save and restore job state
 * @project BISSO E350 Controller
 */

#ifndef JOB_RECOVERY_H
#define JOB_RECOVERY_H

#include <stdint.h>
#include <stdbool.h>

#define RECOVERY_MAGIC 0xBABECAFE

/**
 * @brief Job recovery state saved to NVS
 */
typedef struct {
    uint32_t magic;             // RECOVERY_MAGIC if valid
    char filename[64];          // G-code file path
    uint32_t line_number;       // Last executed line (1-indexed)
    float pos_x;                // Machine position X (mm)
    float pos_y;                // Machine position Y (mm)
    float pos_z;                // Machine position Z (mm)
    float pos_a;                // Machine position A (deg)
    uint8_t wcs_index;          // Active WCS (0=G54, 1=G55, etc.)
    float feed_rate;            // Last feed rate (mm/min)
    uint32_t timestamp;         // Unix timestamp when saved
} job_recovery_t;

/**
 * @brief Initialize recovery system
 * Checks NVS for existing recovery state on boot
 */
void recoveryInit(void);

/**
 * @brief Check if recovery state exists
 * @return true if valid recovery data is available
 */
bool recoveryHasState(void);

/**
 * @brief Get recovery state
 * @param state Pointer to state struct to fill
 * @return true if state retrieved successfully
 */
bool recoveryGetState(job_recovery_t* state);

/**
 * @brief Save current job state to NVS
 * @param filename Current G-code file
 * @param line_number Current line number
 * @param x Current X position
 * @param y Current Y position
 * @param z Current Z position
 * @param a Current A position
 * @param wcs_index Active work coordinate system
 * @param feed_rate Current feed rate
 */
void recoverySaveState(const char* filename, uint32_t line_number,
                       float x, float y, float z, float a,
                       uint8_t wcs_index, float feed_rate);

/**
 * @brief Clear recovery state (call on job complete or abort)
 */
void recoveryClear(void);

/**
 * @brief Print recovery status to CLI
 */
void recoveryPrintStatus(void);

/**
 * @brief Get lines since last save
 * Used to determine when to auto-save
 */
uint32_t recoveryGetLinesSinceSave(void);

/**
 * @brief Increment line counter and auto-save if needed
 * @param filename Current G-code file
 * @param line_number Current line number
 * @param x Current X position
 * @param y Current Y position
 * @param z Current Z position
 * @param a Current A position
 * @param wcs_index Active work coordinate system
 * @param feed_rate Current feed rate
 */
void recoveryCheckAutoSave(const char* filename, uint32_t line_number,
                           float x, float y, float z, float a,
                           uint8_t wcs_index, float feed_rate);

#endif // JOB_RECOVERY_H
