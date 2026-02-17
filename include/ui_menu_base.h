/**
 * @file ui_menu_base.h
 * @brief Reusable base class for physical LCD menus (PosiPro)
 */

#ifndef UI_MENU_BASE_H
#define UI_MENU_BASE_H

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

class BaseMenu {
public:
    BaseMenu(int maxLines = 4) : m_maxLines(maxLines), m_selection(0), m_itemCount(0) {
        clearLines();
    }

    virtual ~BaseMenu() {}

    // Input handlers
    virtual void nextItem() {
        if (m_itemCount > 0) {
            m_selection = (m_selection + 1) % m_itemCount;
        }
    }

    virtual void setSelection(int index) {
        if (index >= 0 && index < m_itemCount) {
            m_selection = index;
        }
    }

    int getSelection() const { return m_selection; }
    int getItemCount() const { return m_itemCount; }

    // Display access
    const char* getLine(int index) const {
        if (index >= 0 && index < m_maxLines) {
            return m_lines[index];
        }
        return "";
    }

protected:
    void clearLines() {
        for (int i = 0; i < m_maxLines; i++) {
            memset(m_lines[i], ' ', 20);
            m_lines[i][20] = '\0';
        }
    }

    void setLine(int index, const char* format, ...) {
        if (index < 0 || index >= m_maxLines) return;
        
        va_list args;
        va_start(args, format);
        vsnprintf(m_lines[index], 21, format, args);
        va_end(args);
        
        // Ensure null termination and pad with spaces if needed (KISS: vsnprintf handles termination)
        size_t len = strlen(m_lines[index]);
        if (len < 20) {
            memset(m_lines[index] + len, ' ', 20 - len);
            m_lines[index][20] = '\0';
        }
    }

    int m_maxLines;
    int m_selection;
    int m_itemCount;
    char m_lines[4][21]; // Standard 20x4 LCD
};

#endif // UI_MENU_BASE_H
