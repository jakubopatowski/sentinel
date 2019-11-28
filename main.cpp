#include "CivetServer.h"
#include "sqlite3.h"
#include <cstring>
#include <iostream>
#include <string>
#include <fstream>
#include "boost/filesystem.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define DOCUMENT_ROOT "."
#define PORT "8081"
#define EXAMPLE_URI "/pomoc"
#define EXIT_URI "/exit"


/* Exit flag for main loop */
volatile bool exitNow = false;

class Db{
public:
    static Db& getInstance(){
        static Db instance;
        return instance;
    }

    int open_db(){
        m_status = sqlite3_open( m_db_name.c_str(), &m_db );
        if( m_status ){
            std::cout << "Nie mo¿na nawi¹zaæ po³¹czenia z baz¹ "
                << m_db_name
                << ": "
                << sqlite3_errmsg( m_db )
                << std::endl;
            sqlite3_close( m_db );
        }
        else{
            std::cout << "Nawi¹zano po³¹czenie z baz¹ "
                << m_db_name
                << std::endl;
        }

        return m_status;
    }

    int close_db(){
        m_status = sqlite3_close( m_db );
        std::cout << "Zamkniêto po³¹czenie z baz¹ " << m_db_name << std::endl;
        return m_status;
    }

    int create_tables(){
        std::string create_help =
            "CREATE TABLE HELP("
            "URL TEXT UNIQUE NOT NULL, "
            "DATA_TYPE INT NOT NULL, "
            "DATA BLOB NOT NULL);";

        char* errmsg;
        m_status = sqlite3_exec( m_db, create_help.c_str(), NULL, 0, &errmsg );
        if( m_status != SQLITE_OK ){
            std::cout << "Error create table HELP !" << std::endl;
        }
        else{
            std::cout << "Table HELP created !" << std::endl;
        }

        return m_status;
    }

    std::string get_blob( const std::string& url ){
        std::string result;
        std::string query =
            "SELECT data FROM HELP where url='" + url + "';";
        sqlite3_stmt* stmt;
      
        m_status = sqlite3_prepare_v2( m_db, query.c_str(), -1, &stmt, 0 );
        if( m_status != SQLITE_OK ){
            std::cout << "sqlite3_prepare_v2 failed: " << sqlite3_errmsg( m_db ) << std::endl;
            return result;
        }
      
        m_status = sqlite3_step( stmt );
        if( m_status != SQLITE_DONE && m_status != SQLITE_ROW ){
            std::cout << "sqlite3_step failed: " << sqlite3_errmsg( m_db ) << std::endl;
            return result;
        }
        
        char* result_tmp = ( char* )sqlite3_column_blob( stmt, 0 );
        if( NULL != result_tmp ){
            result = std::string( result_tmp );
        }

        return result;
    }

    int get_image( const std::string& url, std::vector<char>& data, int& data_size ){
        std::string query =
            "SELECT data FROM HELP where url='" + url + "';";
        sqlite3_stmt* stmt;

        m_status = sqlite3_prepare_v2( m_db, query.c_str(), -1, &stmt, 0 );
        if( m_status != SQLITE_OK ){
            std::cout << "sqlite3_prepare_v2 failed: " << sqlite3_errmsg( m_db ) << std::endl;
            return m_status;
        }

        m_status = sqlite3_step( stmt );
        if( m_status != SQLITE_DONE && m_status != SQLITE_ROW ){
            std::cout << "sqlite3_step failed: " << sqlite3_errmsg( m_db ) << std::endl;
            return m_status;
        }

        data_size = sqlite3_column_bytes( stmt, 0 );
        if( data_size > 0 ){
            std::vector<char> tmp( data_size );
            const char* pBuffer = reinterpret_cast< const char* >( sqlite3_column_blob( stmt, 0 ) );
            std::copy( pBuffer, pBuffer + tmp.size(), &tmp[ 0 ] );
            data = tmp;
        }
        return m_status;
    }

    int insert_blob( const std::string& uri, int data_type, const std::string& file_path ){
        std::ifstream file( file_path, std::ios::in | std::ios::binary );
        if( !file ){
            std::cout << "Nie mo¿na otworzyæ pliku! " << std::endl;
            return SQLITE_ERROR;
        }
        file.seekg( 0, std::ifstream::end );
        std::streampos size = file.tellg();
        file.seekg( 0 );

        char* buffer = new char[ size ];
        file.read( buffer, size );

        sqlite3_stmt* stmt;
        std::string query =
            "INSERT INTO HELP(URL, DATA_TYPE, DATA) VALUES(?, ?, ?);";
        m_status = sqlite3_prepare_v2( m_db, query.c_str(), -1, &stmt, 0 );
        if( m_status != SQLITE_OK ){
            std::cout << "sqlite3_prepare_v2 failed: " << m_status << std::endl;
            return m_status;
        }

        m_status = sqlite3_bind_text( stmt, 1, uri.c_str(), uri.size(), SQLITE_STATIC );
        if( m_status != SQLITE_OK ){
            std::cout << "bind failed: " << sqlite3_errmsg( m_db ) << std::endl;
        }

        m_status = sqlite3_bind_int( stmt, 2, data_type );
        if( m_status != SQLITE_OK ){
            std::cout << "bind failed: " << sqlite3_errmsg( m_db ) << std::endl;
        }

        m_status = sqlite3_bind_text( stmt, 3, buffer, size, SQLITE_STATIC );
        if( m_status != SQLITE_OK ){
            std::cout << "bind failed: " << sqlite3_errmsg( m_db ) << std::endl;
        }

        m_status = sqlite3_step( stmt );
        if( m_status != SQLITE_DONE ){
            std::cout << "execution failed: " << sqlite3_errmsg( m_db ) << std::endl;
        }
        sqlite3_finalize( stmt );
        return m_status;
    }

    Db( Db const& ) = delete;
    void operator=( Db const& ) = delete;

private:
    Db(): m_db_name( "test.db" ){}
    ~Db(){}
private:
    int m_status;
    sqlite3* m_db;
    std::string m_db_name;
};

class Tools
{
public:
    static const char* header_txt() {
        return "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
    }

    static const char* header_image_png(){
        return "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nConnection: close\r\n\r\n";
    }

    static const char* header_image_jpeg(){
        return "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nConnection: close\r\n\r\n";
    }

    static void list_files(){
        boost::filesystem::path p( "c:\\Jopa\\HOME\\Projekty\\sentinel\\help" );
        boost::filesystem::directory_iterator end_itr;

        for( boost::filesystem::directory_iterator itr( p ); itr != end_itr; ++itr ){
            std::string file = itr->path().string();
            std::cout << file << std::endl;
        }
    }
};

class ExampleHandler: public CivetHandler
{
public:
    bool handleGet( CivetServer* server, struct mg_connection* conn )
    {
        mg_printf( conn, Tools::header_txt() );
        mg_printf( conn, "<html><body>\r\n" );
        mg_printf( conn,
                   "<h2>This is an example text from a C++ handler</h2>\r\n" );
        mg_printf( conn,
                   "<p>Poka¿ pomoc dziennika: <a "
                   "href=\"help\">click here</a></p>\r\n" );
        mg_printf( conn,
                   "<p>To see a page from the A handler <a "
                   "href=\"a\">click here</a></p>\r\n" );
        mg_printf( conn,
                   "<form action=\"a\" method=\"get\">"
                   "To see a page from the A handler with a parameter "
                   "<input type=\"submit\" value=\"click here\" "
                   "name=\"param\" \\> (GET)</form>\r\n" );
        mg_printf( conn,
                   "<form action=\"a\" method=\"post\">"
                   "To see a page from the A handler with a parameter "
                   "<input type=\"submit\" value=\"click here\" "
                   "name=\"param\" \\> (POST)</form>\r\n" );
        mg_printf( conn,
                   "<p>To see a page from the A/B handler <a "
                   "href=\"a/b\">click here</a></p>\r\n" );
        mg_printf( conn,
                   "<p>To see a page from the *.foo handler <a "
                   "href=\"xy.foo\">click here</a></p>\r\n" );
        mg_printf( conn,
                   "<p>To see a page from the WebSocket handler <a "
                   "href=\"ws\">click here</a></p>\r\n" );
        mg_printf( conn,
                   "<p>To exit <a href=\"%s\">click here</a></p>\r\n",
                   EXIT_URI );
        mg_printf( conn, "</body></html>\r\n" );
        return true;
    }
};

class HelpHandler: public CivetHandler
{
public:
    bool handleGet( CivetServer* server, struct mg_connection* conn )
    {
        const struct mg_request_info* req_info = mg_get_request_info( conn );
        std::cout << "URL: " << req_info->request_uri << std::endl;

        mg_printf( conn, Tools::header_txt() );
        std::string help = Db::getInstance().get_blob( req_info->request_uri );
        mg_printf( conn, help.c_str() );

        return true;
    }
};

class ImageHandler: public CivetHandler
{
public:
    bool handleGet( CivetServer* server, struct mg_connection* conn)
    {
        const struct mg_request_info* req_info = mg_get_request_info( conn );
        std::cout << "URL: " << req_info->request_uri << std::endl;
       
        mg_printf( conn, Tools::header_image_png() );
        std::vector<char> image;
        int size = 0;
        Db::getInstance().get_image( req_info->request_uri, image, size );
        if( size > 0 ){
            mg_write( conn, reinterpret_cast< char* >( image.data() ), size );
        }
        return true;
    }
};

class ExitHandler: public CivetHandler
{
public:
    bool handleGet( CivetServer server, struct mg_connection* conn )
    {
        mg_printf( conn,
                   "HTTP/1.1 200 OK\r\nContent-Type: "
                   "text/plain\r\nConnection: close\r\n\r\n" );
        mg_printf( conn, "Bye!\n" );
        exitNow = true;
        return true;
    }
};

class AHandler: public CivetHandler
{
private:
    bool handleAll( const char* method,
                   CivetServer* server,
                   struct mg_connection* conn )
    {
        std::string s = "";
        mg_printf( conn,
                   "HTTP/1.1 200 OK\r\nContent-Type: "
                   "text/html\r\nConnection: close\r\n\r\n" );
        mg_printf( conn, "<html><body>" );
        mg_printf( conn, "<h2>This is the A handler for \"%s\" !</h2>", method );
        if ( CivetServer::getParam( conn, "param", s ) ) {
            mg_printf( conn, "<p>param set to %s</p>", s.c_str() );
        }
        else {
            mg_printf( conn, "<p>param not set</p>" );
        }
        mg_printf( conn, "</body></html>\n" );
        return true;
    }

public:
    bool handleGet( CivetServer* server, struct mg_connection* conn )
    {
        return handleAll( "GET", server, conn );
    }
    bool handlePost( CivetServer* server, struct mg_connection* conn )
    {
        return handleAll( "POST", server, conn );
    }
};

class ABHandler: public CivetHandler
{
public:
    bool handleGet( CivetServer* server, struct mg_connection* conn )
    {
        mg_printf( conn,
                   "HTTP/1.1 200 OK\r\nContent-Type: "
                   "text/html\r\nConnection: close\r\n\r\n" );
        mg_printf( conn, "<html><body>" );
        mg_printf( conn, "<h2>This is the AB handler!!!</h2>" );
        mg_printf( conn, "</body></html>\n" );
        return true;
    }
};

class FooHandler: public CivetHandler
{
public:
    bool handleGet( CivetServer* server, struct mg_connection* conn )
    {
        /* Handler may access the request info using mg_get_request_info */
        const struct mg_request_info* req_info = mg_get_request_info( conn );

        mg_printf( conn,
                   "HTTP/1.1 200 OK\r\nContent-Type: "
                   "text/html\r\nConnection: close\r\n\r\n" );

        mg_printf( conn, "<html><body>\n" );
        mg_printf( conn, "<h2>This is the Foo GET handler!!!</h2>\n" );
        mg_printf( conn,
                   "<p>The request was:<br><pre>%s %s HTTP/%s</pre></p>\n",
                   req_info->request_method,
                   req_info->request_uri,
                   req_info->http_version );
        mg_printf( conn, "</body></html>\n" );

        return true;
    }

    bool handlePost( CivetServer* server, struct mg_connection* conn )
    {
        /* Handler may access the request info using mg_get_request_info */
        const struct mg_request_info* req_info = mg_get_request_info( conn );
        long long rlen, wlen;
        long long nlen = 0;
        long long tlen = req_info->content_length;
        char buf[ 1024 ];

        mg_printf( conn,
                   "HTTP/1.1 200 OK\r\nContent-Type: "
                   "text/html\r\nConnection: close\r\n\r\n" );

        mg_printf( conn, "<html><body>\n" );
        mg_printf( conn, "<h2>This is the Foo POST handler!!!</h2>\n" );
        mg_printf( conn,
                   "<p>The request was:<br><pre>%s %s HTTP/%s</pre></p>\n",
                   req_info->request_method,
                   req_info->request_uri,
                   req_info->http_version );
        mg_printf( conn, "<p>Content Length: %li</p>\n", ( long )tlen );
        mg_printf( conn, "<pre>\n" );

        while ( nlen < tlen ) {
            rlen = tlen - nlen;
            if ( rlen > sizeof( buf ) ) {
                rlen = sizeof( buf );
            }
            rlen = mg_read( conn, buf, ( size_t )rlen );
            if ( rlen <= 0 ) {
                break;
            }
            wlen = mg_write( conn, buf, ( size_t )rlen );
            if ( wlen != rlen ) {
                break;
            }
            nlen += wlen;
        }

        mg_printf( conn, "\n</pre>\n" );
        mg_printf( conn, "</body></html>\n" );

        return true;
    }

#define fopen_recursive fopen

    bool handlePut( CivetServer* server, struct mg_connection* conn )
    {
        /* Handler may access the request info using mg_get_request_info */
        const struct mg_request_info* req_info = mg_get_request_info( conn );
        long long rlen, wlen;
        long long nlen = 0;
        long long tlen = req_info->content_length;
        FILE* f;
        char buf[ 1024 ];
        int fail = 0;

#ifdef _WIN32
        _snprintf( buf, sizeof( buf ), "D:\\somewhere\\%s\\%s", req_info->remote_user, req_info->local_uri );
        buf[ sizeof( buf ) - 1 ] = 0;
        if ( strlen( buf ) > 255 ) {
            /* Windows will not work with path > 260 (MAX_PATH), unless we use
             * the unicode API. However, this is just an example code: A real
             * code will probably never store anything to D:\\somewhere and
             * must be adapted to the specific needs anyhow. */
            fail = 1;
            f = NULL;
        }
        else {
            f = fopen_recursive( buf, "wb" );
        }
#else
        snprintf( buf, sizeof( buf ), "~/somewhere/%s/%s", req_info->remote_user, req_info->local_uri );
        buf[ sizeof( buf ) - 1 ] = 0;
        if ( strlen( buf ) > 1020 ) {
            /* The string is too long and probably truncated. Make sure an
             * UTF-8 string is never truncated between the UTF-8 code bytes.
             * This example code must be adapted to the specific needs. */
            fail = 1;
            f = NULL;
        }
        else {
            f = fopen_recursive( buf, "w" );
        }
#endif

        if ( !f ) {
            fail = 1;
        }
        else {
            while ( nlen < tlen ) {
                rlen = tlen - nlen;
                if ( rlen > sizeof( buf ) ) {
                    rlen = sizeof( buf );
                }
                rlen = mg_read( conn, buf, ( size_t )rlen );
                if ( rlen <= 0 ) {
                    fail = 1;
                    break;
                }
                wlen = fwrite( buf, 1, ( size_t )rlen, f );
                if ( wlen != rlen ) {
                    fail = 1;
                    break;
                }
                nlen += wlen;
            }
            fclose( f );
        }

        if ( fail ) {
            mg_printf( conn,
                       "HTTP/1.1 409 Conflict\r\n"
                       "Content-Type: text/plain\r\n"
                       "Connection: close\r\n\r\n" );
        }
        else {
            mg_printf( conn,
                       "HTTP/1.1 201 Created\r\n"
                       "Content-Type: text/plain\r\n"
                       "Connection: close\r\n\r\n" );
        }

        return true;
    }
};

class WsStartHandler: public CivetHandler
{
public:
    bool handleGet( CivetServer* server, struct mg_connection* conn )
    {

        mg_printf( conn,
                   "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: "
                   "close\r\n\r\n" );

        mg_printf( conn, "<!DOCTYPE html>\n" );
        mg_printf( conn, "<html>\n<head>\n" );
        mg_printf( conn, "<meta charset=\"UTF-8\">\n" );
        mg_printf( conn, "<title>Embedded websocket example</title>\n" );

#ifdef USE_WEBSOCKET
        /* mg_printf(conn, "<script type=\"text/javascript\"><![CDATA[\n"); ...
         * xhtml style */
        mg_printf( conn, "<script>\n" );
        mg_printf(
            conn,
            "var i=0\n"
            "function load() {\n"
            "  var wsproto = (location.protocol === 'https:') ? 'wss:' : 'ws:';\n"
            "  connection = new WebSocket(wsproto + '//' + window.location.host + "
            "'/websocket');\n"
            "  websock_text_field = "
            "document.getElementById('websock_text_field');\n"
            "  connection.onmessage = function (e) {\n"
            "    websock_text_field.innerHTML=e.data;\n"
            "    i=i+1;"
            "    connection.send(i);\n"
            "  }\n"
            "  connection.onerror = function (error) {\n"
            "    alert('WebSocket error');\n"
            "    connection.close();\n"
            "  }\n"
            "}\n" );
        /* mg_printf(conn, "]]></script>\n"); ... xhtml style */
        mg_printf( conn, "</script>\n" );
        mg_printf( conn, "</head>\n<body onload=\"load()\">\n" );
        mg_printf(
            conn,
            "<div id='websock_text_field'>No websocket connection yet</div>\n" );
#else
        mg_printf( conn, "</head>\n<body>\n" );
        mg_printf( conn, "Example not compiled with USE_WEBSOCKET\n" );
#endif
        mg_printf( conn, "</body>\n</html>\n" );

        return 1;
    }
};

#ifdef USE_WEBSOCKET
class WebSocketHandler: public CivetWebSocketHandler {

    virtual bool handleConnection( CivetServer* server,
                                   const struct mg_connection* conn ) {
        printf( "WS connected\n" );
        return true;
    }

    virtual void handleReadyState( CivetServer* server,
                                   struct mg_connection* conn ) {
        printf( "WS ready\n" );

        const char* text = "Hello from the websocket ready handler";
        mg_websocket_write( conn, MG_WEBSOCKET_OPCODE_TEXT, text, strlen( text ) );
    }

    virtual bool handleData( CivetServer* server,
                             struct mg_connection* conn,
                             int bits,
                             char* data,
                             size_t data_len ) {
        printf( "WS got %lu bytes: ", ( long unsigned )data_len );
        fwrite( data, 1, data_len, stdout );
        printf( "\n" );

        mg_websocket_write( conn, MG_WEBSOCKET_OPCODE_TEXT, data, data_len );
        return ( data_len < 4 );
    }

    virtual void handleClose( CivetServer* server,
                              const struct mg_connection* conn ) {
        printf( "WS closed\n" );
    }
};
#endif

int main( int argc, char* argv[] )
{
    const char* options[] = {
        "document_root", DOCUMENT_ROOT, "listening_ports", PORT, 0 };

    std::vector<std::string> cpp_options;
    for ( int i = 0; i < ( sizeof( options ) / sizeof( options[ 0 ] ) - 1 ); i++ ) {
        cpp_options.push_back( options[ i ] );
    }

    CivetServer server( cpp_options ); // <-- C++ style start

    ExampleHandler h_ex;
    server.addHandler( EXAMPLE_URI, h_ex );

    ExitHandler h_exit;
    server.addHandler( EXIT_URI, h_exit );

    AHandler h_a;
    //server.addHandler( "/a", h_a );
   
    ABHandler h_ab;
    //server.addHandler( "/a/b", h_ab );

    WsStartHandler h_ws;
    server.addHandler( "/ws", h_ws );

    HelpHandler h_help;
    server.addHandler( "/help", h_help );

    ImageHandler h_image;
    server.addHandler( "**.png", h_image );
    server.addHandler( "**.jpg", h_image );

#ifdef NO_FILES
    /* This handler will handle "everything else", including
     * requests to files. If this handler is installed,
     * NO_FILES should be set. */
    FooHandler h_foo;
    server.addHandler( "", h_foo );

    printf( "See a page from the \"all\" handler at http://localhost:%s/\n", PORT );
#else
    FooHandler h_foo;
    server.addHandler( "**.foo", h_foo );
    //server.addHandler( "/a", h_foo );

    std::cout << "Browse files at http ://localhost:" << PORT << "/" << std::endl;
    //printf( "Browse files at http://localhost:%s/\n", PORT );
#endif

#ifdef USE_WEBSOCKET
    WebSocketHandler h_websocket;
    server.addWebSocketHandler( "/websocket", h_websocket );
    printf( "Run websocket example at http://localhost:%s/ws\n", PORT );
#endif
    std::cout << "Run example at http://localhost:" << PORT << EXAMPLE_URI << std::endl;
    std::cout << "Exit at http://localhost:" << PORT << EXIT_URI << std::endl;

   
    Db::getInstance().open_db();
    Db::getInstance().create_tables();
    Db::getInstance().insert_blob( "/help", 1, "c:\\Jopa\\HOME\\Projekty\\sentinel\\build\\A1-dziennik.htm" );
    Db::getInstance().insert_blob( "/A1-dziennik_pliki/image002.png", 1,
                                   "c:\\Jopa\\HOME\\Projekty\\sentinel\\build\\A1-dziennik_pliki\\image002.png" );

    while ( !exitNow ) {
#ifdef _WIN32
        Sleep( 1000 );
#else
        sleep( 1 );
#endif
    }

    Db::getInstance().close_db();
    std::cout << "Bye!" << std::endl;

    return 0;
}
