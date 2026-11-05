#include <QCoreApplication>

#include <catch2/catch_session.hpp>

// Model tests need a QCoreApplication for queued signals and the event loop.
int main(int argc, char* argv[])
{
    const QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
