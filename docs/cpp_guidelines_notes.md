# C++ Guidelines Development Notes

C++ is a hard language, and everyone can agree on that. Building a real C++ project can be a time-consuming process, let along building your first real C++ project.

Throughout my time as a Competitive Programmer, I've written (extremely) bad C++ code. Those are perfectly working code, and in some cases incredibly performant. However, they suffers from a lot of bad programming habbits (spaghetti codes, embedded constants, scalability) in exchange for quick development speed expected in a programming contest. 

Hence, here lies certain C++ guidelines I've taken notes of throughout my time developing this project. Most of this is gonna be referenced from [The C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines). As some people might say, Just In Time knowledge is a very effective way of learning.

## C: Classes and class hierarchies

### C.2: Use `class` if the class has an invariant; use `struct` if data members can vary independently.

**Reason**: Readability

The use of `class` alerts the programmer the need for an invariant (ie. a **logical condition** a constructor must establish for the public member functions to assume)

Example: DateTime class requires year, month, date must be a valid date.