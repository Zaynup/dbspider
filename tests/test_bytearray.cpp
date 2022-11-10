#include "bytearray.h"
#include "log.h"

using namespace dbspider;

static Logger::ptr g_logger = DBSPIDER_LOG_ROOT();

void test1()
{
    std::string str = "This is a bytearray example!";
    std::vector<int> vec;
    ByteArray byteArray(1);
    for (int i = 0; i < 100; ++i)
    {
        // vec.push_back(rand());
        // byteArray.writeFint32(vec[i]);
        byteArray.writeStringVint(str);
    }
    byteArray.setPosition(0);
    for (int i = 0; i < 100; ++i)
    {
        // int a = byteArray.readFint32();
        std::string tmp = byteArray.readStringVint();
        DBSPIDER_LOG_INFO(g_logger) << tmp;
    }
}

class Student
{
public:
    Student(){};
    Student(bool g, short age, int id, std::string name) : m_isMale(g), m_age(age), m_stuId(id), m_name(name) {}
    std::string toString()
    {
        std::stringstream ss;
        ss << "[" << (m_isMale ? "male" : "female") << "," << m_age << "," << m_stuId << "," << m_name << "]";
        return ss.str();
    }
    void readFromByteArray(ByteArray &byteArray)
    {
        m_isMale = byteArray.readFint8();
        m_age = byteArray.readFint16();
        m_stuId = byteArray.readFint32();
        m_name = byteArray.readStringF16();
    }
    void writeToByteArray(ByteArray &byteArray)
    {
        byteArray.writeFint8(m_isMale);
        byteArray.writeFint16(m_age);
        byteArray.writeFint32(m_stuId);
        byteArray.writeStringF16(m_name);
    }

private:
    bool m_isMale = false;
    short m_age = 0;
    int m_stuId = 0;
    std::string m_name;
};

//序列化、反序列化
void test2()
{
    Student student(true, 20, 201912900, "Zayn");
    ByteArray byteArray{};
    student.writeToByteArray(byteArray);
    byteArray.setPosition(0);
    byteArray.writeToFile("./test_file/Student.dat");

    ByteArray b;
    b.readFromFile("./test_file/Student.dat");
    b.setPosition(0);
    Student s;
    s.readFromByteArray(b);

    DBSPIDER_LOG_INFO(g_logger) << s.toString();
}

void test3()
{
    int n1, n2;
    ByteArray byteArray{};
    byteArray.writeInt32(128);
    byteArray.writeInt32(5);
    byteArray.setPosition(0);
    DBSPIDER_LOG_INFO(g_logger) << byteArray.toHexString();
    n1 = byteArray.readInt32();
    n2 = byteArray.readInt32();
    DBSPIDER_LOG_INFO(g_logger) << n1 << "  " << n2;
    DBSPIDER_LOG_INFO(g_logger) << byteArray.toHexString();
}

int main()
{
    test1();
    test2();
    test3();
}