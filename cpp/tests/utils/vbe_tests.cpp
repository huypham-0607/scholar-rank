#include "scholar_rank/utils/vbe.hpp"

#include <random>
#include <gtest/gtest.h>

unsigned long long rd(unsigned long long l, unsigned long long r, std::mt19937_64 &mt) {
    return std::uniform_int_distribution<unsigned long long> (l,r) (mt);
}

TEST(VBETest, HandlesZero) {
    unsigned char buffer[8];
    memset(buffer,0,sizeof(buffer));
    size_t length = vbe_encode((unsigned long long) 0, buffer);

    ASSERT_EQ(length, 1);
    ASSERT_EQ(buffer[0], 128);

    unsigned long long decoded = vbe_decode(buffer);
    ASSERT_EQ(decoded, 0);
}

TEST(VBETest, Boundary) {
    unsigned long long val = (1LL<<7);
    size_t expected_length = (63 - __builtin_clzll(val))/7+1;
    
    unsigned char buffer[8];
    memset(buffer,0,sizeof(buffer));

    size_t length = vbe_encode(val,buffer);

    ASSERT_EQ(length, expected_length);
    ASSERT_EQ(buffer[0], 0);
    ASSERT_EQ(buffer[1], (1LL<<7) + 1);

    unsigned long long decoded = vbe_decode(buffer);
    ASSERT_EQ(decoded, val);
}

TEST(VBETest, MaxValue) {
    unsigned long long val = (1LL<<56)-1;
    size_t expected_length = (63 - __builtin_clzll(val))/7+1;
    
    unsigned char buffer[8];
    memset(buffer,0,sizeof(buffer));

    size_t length = vbe_encode(val,buffer);

    ASSERT_EQ(length, expected_length);
    for (int i = 0; i < 8; i++){
        ASSERT_EQ(buffer[i], (1<<7)-1 + ((i==7)?(1<<7):0));
    }

    unsigned long long decoded = vbe_decode(buffer);
    ASSERT_EQ(decoded, val);
}

TEST(VBETest, BufferOverflow) {
    unsigned long long val = (1LL<<60);
    
    unsigned char buffer[8];
    memset(buffer,0,sizeof(buffer));

    ASSERT_THROW({
        size_t length = vbe_encode(val,buffer);
    }, std::runtime_error);
}

TEST(VBETest, RandomInput) {
    std:: mt19937_64 mt(42);

    unsigned long long l = 1;
    unsigned long long r = (1LL<<56) - 1;
    int n = 10000;

    unsigned char buffer[8];
    memset(buffer,0,sizeof(buffer));
    for (int i=0; i<n; i++){
        unsigned long long val = rd(l,r,mt);
        size_t expected_length = (63 - __builtin_clzll(val))/7+1;
        size_t length = vbe_encode(val, buffer);

        ASSERT_EQ(length, expected_length);
        
        unsigned long long decoded = vbe_decode(buffer);
        ASSERT_EQ(decoded, val);
    }
}