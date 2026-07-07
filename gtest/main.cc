#include <gtest/gtest.h>

int Add(int a, int b) {
    return a + b;
}
int AddNegative(int a, int b) {
    return a - b;
}

TEST(Test, Add) {
    ASSERT_EQ(Add(5, 5), 9);
    ASSERT_EQ(Add(1, 2), 4);
}
TEST(Test, AddNegative) {
    ASSERT_EQ(AddNegative(-5, -5), -10);
    ASSERT_EQ(AddNegative(-1, -2), -3);
}

int main(int argc, char* argv[]) 
{
    testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
