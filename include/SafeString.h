/* Copyright (c) 2025 Paul Winwood, G8KIG - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPL-Version 3 license.
 *
 * There is a copy of the GPL-Version 3 license in the same folder as this file.
 */

#pragma once

class SafeString
{
public:
    // Constructors
    SafeString();
    SafeString(const char *s);
    SafeString(const char *s, size_t len);
    SafeString(size_t len);
    SafeString(const SafeString &other);

    // Destructor
    virtual ~SafeString();

    // Assignment operator
    SafeString &operator=(const SafeString &other);

    bool operator==(const SafeString &other) const;

    // Character access
    char &operator[](int index);
    const char &operator[](int index) const;

    // Write access
    char *get();

    // Get C-style string
    const char *c_str() const;

    // Get string length
    size_t length() const;

    // Get the current reference count
    int getRefCount() const;

    // Format the string using printf-style formatting
    bool Format(const char *fmt, ...);

private:
    // This struct holds the actual string data and the reference count
    struct StringData
    {
        char *data;
        size_t length;
        int refCount;

        StringData();
        StringData(const char *s, size_t len);
        virtual ~StringData();

        StringData &operator=(const StringData &other) = delete;
    };

    StringData *pData;

    static char EmptyChar;

    void detach(); // Helper for copy-on-write
};
