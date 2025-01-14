//
// GA-SDK-CPP
// Copyright 2018 GameAnalytics C++ SDK. All rights reserved.
//

#include "GAUtilities.h"
#include "GALogger.h"
#include <string.h>
#include <stdio.h>
#include <sstream>
#include <chrono>
#include <regex>
#include <climits>
#include <cctype>

#include <hmac_sha2.h>
#include <guid.h>

#include "stacktrace/call_stack.hpp"

// From crypto
#define MINIZ_HEADER_FILE_ONLY
#include "miniz.c"

namespace gameanalytics
{
    namespace utilities
    {
        std::string printArray(StringVector const& v, std::string const& delim)
        {
            if(v.empty())
                return "";

            if(v.size() == 1)
                return v.front();

            std::string s;
            for(size_t i = 0; i < (v.size() - 1); ++i)
            {
                s += v[i];
                s += delim;
            }

            s += v.back();

            return s;
        }

        int64_t getNumberFromCache(json& node, std::string const& key, int64_t defValue)
        {
            try
            {
                if(node.contains(key))
                {
                    if(node[key].is_number())
                        return utilities::getOptionalValue<int64_t>(node, key, defValue);

                    else if(node[key].is_string())
                    {
                        std::string val = utilities::getOptionalValue<std::string>(node, key, "");
                        if(!val.empty())
                        {
                            return std::stoll(val);
                        }
                    }
                }
            }
            catch(...)
            {
                // just return the default value on failure
            }
            
            return defValue;
        }

        std::pair<std::string, int32_t> getRelevantFunctionFromCallStack()
        {
            std::string function;
            int32_t line = -1;

            try
            {
                stacktrace::call_stack st;
                for(auto& entry : st.stack)
                {
                    std::string f = entry.function;

                    if(f.find("GameAnalytics") == std::string::npos && 
                       f.find("call_stack") == std::string::npos && 
                       f.find("getRelevantFunctionFromCallStack") == std::string::npos)
                    {
                        function = f;
                        line = entry.line;
                        break;
                    }
                }
            }
            catch(...)
            {
                function = "";
                line = -1;
            }

            return std::make_pair(function, line);
        }

        // Compress a STL string using zlib with given compression level and return the binary data.
        // Note: the zlib header is supressed
        static std::vector<char> deflate_string(const char* str, int compressionlevel = Z_BEST_COMPRESSION)
        {
            // z_stream is zlib's control structure
            z_stream zs;
            memset(&zs, 0, sizeof(zs));

            /* windowsize is negative to suppress Zlib header */
            if (Z_OK != deflateInit2(&zs, compressionlevel, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY))
            {
                throw(std::runtime_error("deflateInit failed while compressing."));
            }

            zs.next_in = (Bytef*)str;
            //zs.next_in = reinterpret_cast<Bytef*>(str.data());

            // set the z_stream's input
            zs.avail_in = static_cast<unsigned int>(strlen(str));
            int ret;
            
            constexpr size_t BUFFER_SIZE = 32768;
            static char outbuffer[BUFFER_SIZE];
            std::vector<char> outstring;

            // retrieve the compressed bytes blockwise
            do
            {
                zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
                zs.avail_out = sizeof(outbuffer);

                ret = deflate(&zs, Z_FINISH);

                if (outstring.size() < zs.total_out)
                {
                    size_t s = zs.total_out - outstring.size();
                    // append the block to the output string
                    for(size_t i = 0; i < s; ++i)
                    {
                        outstring.push_back(outbuffer[i]);
                    }
                }
            } while (ret == Z_OK);

            deflateEnd(&zs);

            if (ret != Z_STREAM_END)
            {
                // an error occurred that was not EOF
                logging::GALogger::e("Exception during zlib compression: (%d) %s", ret, zs.msg);
                outstring.clear();
            }

            return outstring;
        }

        // TODO(nikolaj): explain template?
        template <typename T>
        T swap_endian(T u)
        {
            static_assert (CHAR_BIT == 8, "CHAR_BIT != 8");

            union
            {
                T u;
                unsigned char u8[sizeof(T)];
            } source, dest;

            source.u = u;

            for (size_t k = 0; k < sizeof(T); k++)
                dest.u8[k] = source.u8[sizeof(T) - k - 1];

            return dest.u;
        }

        uint32 htonl2(uint32 v)
        {
            uint32 result = 0;
            result |= (v & 0xFF000000) >> 24;
            result |= (v & 0x00FF0000) >> 8;
            result |= (v & 0x0000FF00) << 8;
            result |= (v & 0x000000FF) << 24;

            return result;
        }

        uint32 to_little_endian(uint32 v)
        {
            // convert to big endian
            v = htonl2(v);

            // and to little endian, because gzip wants it so.
            v = swap_endian(v);

            return v;
        }

        constexpr char nb_base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

        int GAUtilities::base64_needed_encoded_length(int length_of_data)
        {
            int nb_base64_chars = (length_of_data + 2) / 3 * 4;

            return nb_base64_chars +               /* base64 char incl padding */
            (nb_base64_chars - 1) / 76 +    /* newlines */
            1;                              /* NUL termination of string */
        }

        /**
         * buf_ is allocated by malloc(3).The size is grater than nb_base64_needed_encoded_length(src_len).
         */
        void GAUtilities::base64_encode(const unsigned char * src, int src_len, unsigned char *buf_)
        {
            unsigned char *buf = buf_;
            int i = 0;
            int j = 0;
            unsigned char char_array_3[3] = {0};
            unsigned char char_array_4[4] = {0};

            while (src_len--)
            {
                char_array_3[i++] = *(src++);
                if (i == 3)
                {
                    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                    char_array_4[1] =
                    ((char_array_3[0] & 0x03) << 4) +
                    ((char_array_3[1] & 0xf0) >> 4);
                    char_array_4[2] =
                    ((char_array_3[1] & 0x0f) << 2) +
                    ((char_array_3[2] & 0xc0) >> 6);
                    char_array_4[3] = char_array_3[2] & 0x3f;
                    for (i = 0; (i < 4); i++) {
                        *buf++ = nb_base64_chars[char_array_4[i]];
                    }
                    i = 0;
                }
            }

            if (i)
            {
                for (j = i; j < 3; j++)
                {
                    char_array_3[j] = '\0';
                }

                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] =
                ((char_array_3[0] & 0x03) << 4) +
                ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] =
                ((char_array_3[1] & 0x0f) << 2) +
                ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;

                for (j = 0; (j < i + 1); j++)
                {
                    *buf++ = nb_base64_chars[char_array_4[j]];
                }

                while ((i++ < 3))
                {
                    *buf++ = '=';
                }
            }
            *buf++ = '\0';
        }

        // gzip compresses a string
        static std::vector<uint8> compress_string_gzip(const char* str, int compressionlevel = Z_BEST_COMPRESSION)
        {
            // https://tools.ietf.org/html/rfc1952
            std::vector<char> deflated = deflate_string(str, compressionlevel);

            constexpr char gzip_header[10] =
            { '\037', '\213', Z_DEFLATED, 0,
                0, 0, 0, 0, /* mtime */
                0, 0x03 /* Unix OS_CODE */
            };

            // Note: apparently, the crc is never validated on ther server side. So I'm not sure, if I have to convert it to little endian.
            uint32_t crc = to_little_endian(crc32(0, (unsigned char*)str, strlen(str)));
            uint32 size  = to_little_endian(static_cast<unsigned int>(strlen(str)));

            size_t totalSize = sizeof(gzip_header);
            totalSize += deflated.size();
            totalSize += 8;

            std::unique_ptr<char[]> resultArray = std::make_unique<char[]>(totalSize);
            
            size_t current = 0;
            for(size_t i = 0; i < sizeof(gzip_header); ++i)
            {
                resultArray[i] = gzip_header[i];
            }
            current += sizeof(gzip_header);
            for(size_t i = 0; i < deflated.size(); ++i)
            {
                resultArray[i + current] = deflated[i];
            }
            current += deflated.size();
            strncpy(resultArray.get() + current, (const char*)&crc, 4);
            current += 4;
            strncpy(resultArray.get() + current, (const char*)&size, 4);

            std::vector<uint8_t> result;

            for(size_t i = 0; i < totalSize; ++i)
            {
                result.push_back(resultArray[i]);
            }

            return result;
        }

        std::string GAUtilities::generateUUID()
        {
            xg::Guid guid = xg::newGuid();
            std::stringstream stream;
            stream << guid;
            return stream.str();
        }

        // TODO(nikolaj): explain function
        void GAUtilities::hmacWithKey(const char* key, const std::vector<uint8_t>& data, std::vector<uint8_t>& out)
        {
#if USE_UWP
            using namespace Platform;
            using namespace Windows::Security::Cryptography::Core;
            using namespace Windows::Security::Cryptography;

            auto keyString = ref new String(utilities::GAUtilities::s2ws(key).c_str());
            auto alg = MacAlgorithmProvider::OpenAlgorithm(MacAlgorithmNames::HmacSha256);
            Platform::Array<unsigned char>^ byteArray = ref new Platform::Array<unsigned char>(static_cast<unsigned int>(data.size()));
            for (size_t i = 0; i < data.size(); ++i)
            {
                byteArray[static_cast<int>(i)] = data[static_cast<int>(i)];
            }
            auto dataBuffer = CryptographicBuffer::CreateFromByteArray(byteArray);
            auto secretKeyBuffer = CryptographicBuffer::ConvertStringToBinary(keyString, BinaryStringEncoding::Utf8);
            auto hmacKey = alg->CreateKey(secretKeyBuffer);

            auto hashedJsonBuffer = CryptographicEngine::Sign(hmacKey, dataBuffer);
            auto hashedJsonBase64 = CryptographicBuffer::EncodeToBase64String(hashedJsonBuffer);
            snprintf(out, 129, "%s", utilities::GAUtilities::ws2s(hashedJsonBase64->Data()).c_str());
#else
            unsigned char mac[SHA256_DIGEST_SIZE];
            hmac_sha256_2(
                (unsigned char*)key,
                strlen(key),
                (unsigned char*)data.data(),
                data.size(),
                mac,
                SHA256_DIGEST_SIZE
            );
            int output_size = base64_needed_encoded_length(SHA256_DIGEST_SIZE);
            out.resize(output_size);
            
            GAUtilities::base64_encode(mac, SHA256_DIGEST_SIZE, out.data());
#endif
        }

        // TODO(nikolaj): explain function
        bool GAUtilities::stringMatch(std::string const& string, std::string const& pattern)
        {
            try
            {
                std::regex expression(pattern);
                return std::regex_match(string, expression);
            }
            catch (const std::regex_error& e)
            {
                logging::GALogger::e("failed to parse regular expression '%s', code: %d, what: %s", pattern.c_str(), e.code(), e.what());

                #if _DEBUG
                    throw;
                #else
                    return true;
                #endif
            }
        }

        std::vector<uint8_t> GAUtilities::gzipCompress(const char* data)
        {
            return compress_string_gzip(data);
        }

        // TODO(nikolaj): explain function
        bool GAUtilities::stringVectorContainsString(const StringVector& vector, std::string const& str)
        {
            for (std::string const& s : vector)
            {
                if (s == str)
                    return true;
            }

            return false;
        }

        // using std::chrono to get time
        int64_t GAUtilities::timeIntervalSince1970()
        {
            return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }

        bool GAUtilities::isStringNullOrEmpty(const char* s)
        {
            return !s || strlen(s) == 0;
        }
    }
}
