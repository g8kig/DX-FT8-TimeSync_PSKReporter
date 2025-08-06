/* Copyright (c) 2025 Paul Winwood, G8KIG - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPL-Version 3 license.
 *
 * There is a copy of the GPL-Version 3 license in the same folder as this file.
 */

#include <cstring>
#include <cstdarg>
#include <cstdio>

#include "SafeString.h"

char SafeString::EmptyChar = 0;

SafeString::StringData::StringData()
    : data(NULL), length(0), refCount(1)
{
}

SafeString::StringData::StringData(const char *s, size_t len)
    : length(len), refCount(1)
{
    // note: assumes that new does not throw an exception
    data = new char[length + 1];
    if (data != NULL)
    {
        if (s != NULL)
        {
            memcpy(data, s, length);
            data[length] = 0;
        }
        else
        {
            memset(data, 0, length + 1);
        }
    }
}

SafeString::StringData::~StringData()
{
    delete[] data;
}

SafeString::SafeString()
{
    // note: assumes that new does not throw an exception
    pData = new StringData();
}

SafeString::SafeString(const char *s)
{
    // note: assumes that new does not throw an exception
    if (s != NULL)
    {
        pData = new StringData(s, strlen(s));
    }
    else
    {
        pData = new StringData();
    }
}

SafeString::SafeString(const char *s, size_t len)
    : pData(NULL)
{
    // note: assumes that new does not throw an exception
    if (s != NULL)
    {
        pData = new StringData(s, len);
    }
    else
    {
        pData = new StringData();
    }
}

SafeString::SafeString(size_t len)
{
    // note: assumes that new does not throw an exception
    pData = new StringData(NULL, len);
}

SafeString::SafeString(const SafeString &other)
    : pData(other.pData)
{
    pData->refCount++;
}

SafeString::~SafeString()
{
    if (--pData->refCount == 0)
    {
        delete pData;
    }
}

SafeString &SafeString::operator=(const SafeString &other)
{
    // Check for self-assignment
    if (this == &other)
    {
        return *this;
    }

    // Decrement the old reference count
    if (--pData->refCount == 0)
    {
        delete pData;
    }

    // Point to the new data and increment its reference count
    pData = other.pData;
    pData->refCount++;

    return *this;
}

// Copy-on-write implementation
void SafeString::detach()
{
    if (pData->refCount > 1)
    {
        // note: assumes that new does not throw an exception
        StringData *newData = new StringData(pData->data, pData->length);
        --pData->refCount;
        pData = newData;
    }
}

char &SafeString::operator[](int index)
{
    if (index < 0 || (size_t)index >= pData->length)
    {
        return EmptyChar;
    }

    detach();
    return pData->data[index];
}

char *SafeString::get()
{
    detach();
    return pData->data;
}

const char &SafeString::operator[](int index) const
{
    if (index < 0 || (size_t)index > pData->length)
    {
        return EmptyChar;
    }

    return pData->data[index];
}

const char *SafeString::c_str() const
{
    return pData->data;
}

size_t SafeString::length() const
{
    return pData->length;
}

int SafeString::getRefCount() const
{
    return pData->refCount;
}

bool SafeString::Format(const char *fmt, ...)
{
    // Estimate required buffer size
    va_list args, args_size;
    va_start(args, fmt);
    va_copy(args_size, args);
    int size = vsnprintf(nullptr, 0, fmt, args_size);
    va_end(args_size);

    if (size < 0)
    {
        va_end(args);
        return false;
    }

    detach(); // Ensure unique copy before modifying

    if (pData->length >= (size_t)size)
    {
        vsnprintf(pData->data, size + 1, fmt, args);
    }
    else
    {
        // Allocate buffer (+1 for null terminator)
        SafeString temp(size);
        vsnprintf(temp.pData->data, size + 1, fmt, args);
        *this = temp;
    }
    va_end(args);
    return true;
}

bool SafeString::operator==(const SafeString &other) const
{
    return pData->length == other.pData->length &&
           memcmp(pData->data, other.pData->data, pData->length) == 0;
}
