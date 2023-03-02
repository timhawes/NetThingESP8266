/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Densaugeo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * Base64 encoding and decoding of strings. Uses '+' for 62, '/' for 63, '=' for padding
 */

#ifndef BASE64_H_INCLUDED
#define BASE64_H_INCLUDED

/* binary_to_base64:
 *   Description:
 *     Converts a single byte from a binary value to the corresponding base64 character
 *   Parameters:
 *     v - Byte to convert
 *   Returns:
 *     ascii code of base64 character. If byte is >= 64, then there is not corresponding base64 character
 *     and 255 is returned
 */
unsigned char binary_to_base64(unsigned char v);

/* base64_to_binary:
 *   Description:
 *     Converts a single byte from a base64 character to the corresponding binary value
 *   Parameters:
 *     c - Base64 character (as ascii code)
 *   Returns:
 *     6-bit binary value
 */
unsigned char base64_to_binary(unsigned char c);

/* encode_base64_length:
 *   Description:
 *     Calculates length of base64 string needed for a given number of binary bytes
 *   Parameters:
 *     input_length - Amount of binary data in bytes
 *   Returns:
 *     Number of base64 characters needed to encode input_length bytes of binary data
 */
unsigned int encode_base64_length(unsigned int input_length);

/* decode_base64_length:
 *   Description:
 *     Calculates number of bytes of binary data in a base64 string
 *   Parameters:
 *     input - Base64-encoded null-terminated string
 *   Returns:
 *     Number of bytes of binary data in input
 */
unsigned int decode_base64_length(unsigned char input[]);

/* encode_base64:
 *   Description:
 *     Converts an array of bytes to a base64 null-terminated string
 *   Parameters:
 *     input - Pointer to input data
 *     input_length - Number of bytes to read from input pointer
 *     output - Pointer to output string. Null terminator will be added automatically
 *   Returns:
 *     Length of encoded string in bytes (not including null terminator)
 */
unsigned int encode_base64(unsigned char input[], unsigned int input_length, unsigned char output[]);

/* decode_base64:
 *   Description:
 *     Converts a base64 null-terminated string to an array of bytes
 *   Parameters:
 *     input - Pointer to input string
 *     output - Pointer to output array
 *   Returns:
 *     Number of bytes in the decoded binary
 */
unsigned int decode_base64(unsigned char input[], unsigned char output[]);

#endif // ifndef
