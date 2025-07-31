#include <iostream>

#include "SafeString.h"
#include "PskReporter.h

// Stream insertion operator
std::ostream &operator<<(std::ostream &os, const SafeString &s)
{
    os << s.c_str();
    return os;
}

void print_info(const char *name, const SafeString &s)
{
    std::cout << name << ": \"" << s << "\" (Length: " << s.length()
              << ", Ref Count: " << s.getRefCount() << ")" << std::endl;
}

int main()
{
    std::cout << "--- Creating s1 ---" << std::endl;
    SafeString s1 = "Hello, World!";
    print_info("s1", s1);
    std::cout << std::endl;

    std::cout << "--- Creating s2 as a copy of s1 ---" << std::endl;
    SafeString s2 = s1; // Copy constructor
    print_info("s1", s1);
    print_info("s2", s2);
    std::cout << std::endl;

    std::cout << "--- Creating s3 and assigning s1 to it ---" << std::endl;
    SafeString s3;
    s3 = s1; // Assignment operator
    print_info("s1", s1);
    print_info("s2", s2);
    print_info("s3", s3);
    std::cout << std::endl;

    std::cout << "--- Modifying s3 (triggers copy-on-write) ---" << std::endl;
    s3[7] = 'C';
    s3[8] = '+';
    s3[9] = '+';
    print_info("s1", s1);

    print_info("s2", s2);
    print_info("s3", s3);
    std::cout << std::endl;

    std::cout << "--- s2 goes out of scope ---" << std::endl;
    {
        SafeString s4 = s1;
        print_info("s1 (inside scope)", s1);
        print_info("s4 (inside scope)", s4);
    }

    print_info("s1 (outside scope)", s1);
    std::cout << std::endl;

    PskReporter reporter("G8KIG/P", "IO91iq", "Test FT8 Xceiver");
    reporter.addReceiveRecord("WBA2CDE", 14074000, 0);
    reporter.send();

    std::cout << "--- Program finished ---" << std::endl;
    return 0;
}
