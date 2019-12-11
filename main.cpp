#include "CivetServer.h"
#include "sqlite3.h"
#include <cstring>
#include <unordered_set>
#include <set>
#include <iostream>
#include <string>
#include <fstream>
#include <thread>
#include <chrono>
#include "boost/filesystem.hpp"
#include "boost/regex.hpp"
#include "rdb.hpp"
#include "rdblob.hpp"
#include "rsystem.hpp"
#include "roptions.hpp"

#define DB_CONFIG_FILE_PATH "/ini/netsystem.ini"
#define SERVICE_CONFIG_FILE_PATH "/ini/rvs_help.ini"

#define DOCUMENT_ROOT "."
#define PORT "8081"
#define EXAMPLE_URI "/pomoc"
#define EXIT_URI "/exit"

using namespace boost::filesystem;
using namespace std;

/* Exit flag for main loop */
volatile bool exitNow = false;

enum class ContentType{ None, Page, Image_png, Image_jpg, Image_gif };

struct Configuration{
    std::string connectionString;
    std::string user;
    std::string host;
    std::string pass;
};

void ReadDbConfig( Configuration& config ){
    // Zaczytanie pliku konfiguracyjnego
    QString configFileName( RSystem::getActiveProject() + DB_CONFIG_FILE_PATH );
    ROptions configManager( configFileName, "" );

    config.user = configManager.get( QString::null, "dbusr" ).ascii();
    config.host = configManager.get( QString::null, "s.1.dbname" ).ascii();
    config.pass = configManager.get( QString::null, "dbpwd" ).ascii();

    config.connectionString = config.user + "/" + config.pass + "@" + config.host;
}

class IDb{
public:
    virtual int open_db( const std::string& connection_str ) = 0;
    virtual int close_db() = 0;
    virtual int create_tables() = 0;
    virtual int get_content( const std::string& url, std::vector<char>& data, int& data_size, ContentType& type ) = 0;
    virtual int put_content( const std::string& uri, int data_type, const std::string& topic, const std::string& file_path ) = 0;
    virtual int get_all_urls( const ContentType& type, std::vector<std::string>& urls ) = 0;

protected:
    IDb(): create_help( "CREATE TABLE baza_pomocy_dane("
                        "id INTEGER NOT NULL,"
                        "url VARCHAR(1024) NOT NULL,"
                        "tytul VARCHAR(1024),"
                        "temat INT,"
                        "typ INT NOT NULL,"
                        "dane BLOB NOT NULL,"
                        "PRIMARY KEY (id),"
                        "UNIQUE(url))" ),
        create_help_seq( "CREATE SEQUENCE baza_pomocy_dane_seq "
                         "START WITH 1 INCREMENT BY 1 NOMAXVALUE NOMINVALUE "
                         "NOCYCLE NOCACHE ORDER" ),
        create_help_trg( "CREATE OR REPLACE TRIGGER baza_pomocy_dane_trg BEFORE INSERT ON baza_pomocy_dane\n"
                         "FOR EACH ROW\n"
                         "BEGIN\n"
                         "  IF :NEW.id IS NULL THEN\n"
                         "    SELECT baza_pomocy_dane_seq.NEXTVAL INTO :NEW.id FROM dual;\n"
                         "  END IF;\n"
                         "END;" ),
        create_topic( "CREATE TABLE baza_pomocy_temat("
                      "id INTEGER NOT NULL,"
                      "temat VARCHAR(1024) NOT NULL,"
                      "PRIMARY KEY (id),"
                      "UNIQUE(temat))" ),
        create_topic_seq( "CREATE SEQUENCE baza_pomocy_temat_seq "
                          "START WITH 1 INCREMENT BY 1 NOMAXVALUE NOMINVALUE "
                          "NOCYCLE NOCACHE ORDER" ),
        create_topic_trg( "CREATE OR REPLACE TRIGGER baza_pomocy_temat_trg BEFORE INSERT ON baza_pomocy_temat\n"
                          "FOR EACH ROW\n"
                          "BEGIN\n"
                          "  IF :NEW.id IS NULL THEN\n"
                          "    SELECT baza_pomocy_temat_seq.NEXTVAL INTO :NEW.id FROM dual;\n"
                          "  END IF;\n"
                          "END;" ),
        get_data( "SELECT dane, typ FROM BAZA_POMOCY_DANE "
                  "where url=:url<char[1024]>" ),
                  //"where title is not null" ),
        put_data( "insert into baza_pomocy_dane(url, typ, temat, dane) "
                  "values(:url<char[1024]>,:data_type<int>,:topic<char[1024]>,empty_blob()) "
                  "returning dane into :blob_data<blob>" ),
        get_urls( "SELECT url FROM baza_pomocy_dane WHERE typ=:data_type<int>" ),
        get_topic_id( "select id from baza_pomocy_temat where temat=:temat<char[1024]>" ),
        get_data_by_topic( "select url, typ from baza_pomocy_dane where temat=:temat<int>" ){}

    std::string create_help;
    std::string create_help_seq;
    std::string create_help_trg;
    std::string create_topic;
    std::string create_topic_seq;
    std::string create_topic_trg;
    std::string get_data;
    std::string put_data;
    std::string get_urls;
    std::string get_topic_id;
    std::string get_data_by_topic;
};

class DbOracle: public IDb{
public:

    static DbOracle& getInstance(){
        static DbOracle instance;
        return instance;
    }

    virtual int open_db( const std::string& connection_str ){
        QString tmp( connection_str.c_str() );
        cout << "Connecting to: " << connection_str << endl;
        
        try{
            m_db = unique_ptr<RDb>( new RDb( RDb::ORACLE_U ) );
            m_db->logon( tmp );
        }
        catch( RDbException & exc ){
            cout << "RDbException EXCEPTION !!!: " << exc.message.ascii() << endl;
        }
        catch( ... ){
            cout << "UNKNOWN EXCEPTION !!!" << endl;
        }

        return 0;
    }

    virtual int close_db(){
        m_db->logoff();
        return 0;
    }

    int run_query( const std::string& query ){
        try{
            RDbStream dbs( *m_db );
            dbs.open( 1, query.c_str() );
        }
        catch( RDbException & exc ){
            cout << "RDbException EXCEPTION !!!: " << endl;
            cout << "    " << query << endl;
            cout << "    " << exc.message.ascii() << endl;
        }
        catch( ... ){
            cout << "UNKNOWN EXCEPTION !!!" << endl;
            cout << "    " << query << endl;
        }

        return 0;
    }

    virtual int create_tables(){
        DbOracle::getInstance().run_query( create_help );
        DbOracle::getInstance().run_query( create_help_seq );
        DbOracle::getInstance().run_query( create_help_trg );

        DbOracle::getInstance().run_query( create_topic );
        DbOracle::getInstance().run_query( create_topic_seq );
        DbOracle::getInstance().run_query( create_topic_trg );

        DbOracle::getInstance().run_query( "insert into baza_pomocy_temat (temat) values ('dziennik')" );
        DbOracle::getInstance().run_query( "insert into baza_pomocy_temat (temat) values ('mapa')" );
        DbOracle::getInstance().run_query( "insert into baza_pomocy_temat (temat) values ('lista_stanow')" );
        DbOracle::getInstance().run_query( "insert into baza_pomocy_temat (temat) values ('przekazanie_zmiany')" );
        DbOracle::getInstance().run_query( "insert into baza_pomocy_temat (temat) values ('lista_ostrzegawcza')" );

        return 0;
    }

    virtual int get_content( const std::string& url, std::vector<char>& data, int& data_size, ContentType& type ){
        QString url_tmp( url.c_str() );
        QString query( get_data.c_str() );
        BlobInputStream data_tmp;
        std::string blob_tmp;
        int type_tmp;

        try{
            RDbStream dbs( *m_db );
            dbs.open( 1, query );
            dbs << url_tmp;
            if( dbs.eof() ){
                cout << "Nie ma urla: " << url << endl;
                return 1;
            }
            dbs >> data_tmp;
            dbs >> type_tmp;
            data_tmp.readAll( blob_tmp );
            data_tmp.close();
            dbs.flush();
        }
        catch( RDbException & exc ){
            cout << "RDbException EXCEPTION !!!" << endl;
            cout << "message: " << exc.message.toStdString() << endl;
            cout << "statement: " << exc.statement.toStdString() << endl;
            cout << "sqlstate: " << exc.sqlstate.toStdString() << endl;
        }
        catch( ... ){
            cout << "UNKNOWN EXCEPTION !!!" << endl;
        }
        
        std::vector<char> blob( blob_tmp.begin(), blob_tmp.end() );
        data = blob;
        data_size = blob.size();
        type = static_cast< ContentType >( type_tmp );

        return 0;
    }

    virtual int put_content( const std::string& url, 
                             int data_type,
                             const std::string& topic,
                             const std::string& file_path ){
        
        std::ifstream file( file_path, std::ios::in | std::ios::binary );
        if( !file ){
            std::cout << "Nie mo¿na otworzyæ pliku! " << std::endl;
            return 1;
        }
        file.seekg( 0, std::ifstream::end );
        std::streampos size = file.tellg();
        file.seekg( 0 );

        char* buffer = new char[ size ];
        file.read( buffer, size );

        QString query( put_data.c_str() );
        QString url_tmp( url.c_str() );

        try{
            RDbStream rdb( *m_db );
            rdb.open( 1, query, RDbStream::LOB_WRITING );
            BlobOutputStream blob;
            rdb << url_tmp;
            rdb << data_type;
            rdb << topic;
            rdb << blob;
            blob.write( buffer, size );
            blob.close();
            rdb.flush();
        }
        catch( RDbException & exc ){
            cout << "RDbException EXCEPTION !!!: " << exc.message.ascii() << endl;
        }
        catch( ... ){
            cout << "UNKNOWN EXCEPTION !!!" << endl;
        }

        return 0;
    }

    virtual int get_all_urls( const ContentType& type, std::vector<std::string>& urls ){
        try{
            RDbStream dbs( *m_db );
            dbs.open( 1, get_urls.c_str() );
            dbs << static_cast< int >( type );
            QString url_tmp;
            while( dbs.eof() ){
                dbs >> url_tmp;
                urls.push_back( url_tmp.toStdString() );
            }
        }
        catch( RDbException & exc ){
            cout << "RDbException EXCEPTION !!!: " << exc.message.ascii() << endl;
        }
        catch( ... ){
            cout << "UNKNOWN EXCEPTION !!!" << endl;
        }

        return 0;
    }

    int get_content_by_topic( const std::string& url, std::vector<std::string>& content_list ){
        QString topic( url.c_str() );
        int topic_id = 0;
        
        QString url_tmp;
        int type_tmp;

        try{
            RDbStream dbs( *m_db );
            dbs.open( 1, get_topic_id.c_str() );
            dbs << topic;
            if( !dbs.eof() ){
                dbs >> topic_id;
            }

            dbs.flush();

            dbs.open( 1, get_data_by_topic.c_str() );
            dbs << topic_id;
            while( !dbs.eof() ){
                dbs >> url_tmp;
                dbs >> type_tmp;
                auto type = static_cast< ContentType >( type_tmp );
                if( type == ContentType::Page ){
                    content_list.push_back( url_tmp.toStdString() );
                }
            }
        }
        catch( RDbException & exc ){
            cout << "RDbException EXCEPTION !!!: " << exc.message.ascii() << endl;
        }
        catch( ... ){
            cout << "UNKNOWN EXCEPTION !!!" << endl;
        }
        return 0;
    }

private:
    std::unique_ptr<RDb> m_db;
};

class DbSqlite3: public IDb{
public:
    static DbSqlite3& getInstance(){
        static DbSqlite3 instance;
        return instance;
    }

    virtual int open_db( const std::string& connection_str ){
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

    virtual int close_db(){
        m_status = sqlite3_close( m_db );
        std::cout << "Zamkniêto po³¹czenie z baz¹ " << m_db_name << std::endl;
        return m_status;
    }

    virtual int create_tables(){
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

    int get_page( const std::string& url, string& page ){
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
        
        char* result_tmp = ( char* )sqlite3_column_blob( stmt, 0 );
        if( NULL != result_tmp ){
            page = std::string( result_tmp );
        }
        sqlite3_finalize( stmt );
        return m_status;
    }

    int get_all_urls( const ContentType& type, std::vector<std::string>& urls )
    {
        std::string query =
            "SELECT url FROM help WHERE data_type=" + to_string( static_cast< int >( type ) ) + ";";
        sqlite3_stmt* stmt;

        m_status = sqlite3_prepare_v2( m_db, query.c_str(), -1, &stmt, 0 );
        if( m_status != SQLITE_OK ){
            std::cout << "sqlite3_prepare_v2 failed: " << sqlite3_errmsg( m_db ) << std::endl;
            return m_status;
        }

        m_status = sqlite3_step( stmt );
        while( m_status == SQLITE_ROW ){
            std::string tmp = ( const char* )sqlite3_column_text( stmt, 0 );
            urls.push_back( tmp );
            m_status = sqlite3_step( stmt );
        }
        if( m_status != SQLITE_DONE && m_status != SQLITE_ROW ){
            std::cout << "sqlite3_step failed: " << sqlite3_errmsg( m_db ) << std::endl;
            return m_status;
        }

        sqlite3_finalize( stmt );
        return m_status;
    }

    int get_content( const std::string& url, std::vector<char>& data, int& data_size, ContentType& type){
        std::string query =
            "SELECT data, data_type FROM HELP where url='" + url + "';";
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

        type = static_cast< ContentType >( sqlite3_column_int( stmt, 1 ) );
        m_status = sqlite3_finalize( stmt );

        return m_status;
    }

    virtual int put_content( const std::string& uri, int data_type, const std::string& topic, const std::string& file_path ){
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

    DbSqlite3( DbSqlite3 const& ) = delete;
    void operator=( DbSqlite3 const& ) = delete;

private:
    DbSqlite3(): m_db_name( "test.db" ){}
    ~DbSqlite3(){}
private:
    int m_status;
    sqlite3* m_db;
    std::string m_db_name;
};

struct Content{

    string url;
    string topic;
    path path;
    ContentType type;
};

class Tools{
public:
    static const char* header_txt(){
        return "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
    }

    static const char* header_image_png(){
        return "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nConnection: close\r\n\r\n";
    }

    static const char* header_image_jpeg(){
        return "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nConnection: close\r\n\r\n";
    }

    static void prepare_link_page( const std::string& topic, 
                                          const std::vector<std::string>& urls,
                                          std::string& result){
        result += header_txt();
        result += "<html><body>\r\n";
        result += "<h2>Pomoc na wybrany temat: " + topic + "</h2>\r\n";

        for( auto url : urls ){
            result += "<p>link: <a href=\"" + url + "\">" + url + "</a></p>\r\n";
        }

        result += "</html></body>\r\n";
    }

    static set<path> list_files( const path& dir ){
        static set<path> result;
        static set<string> file_to_blob_filter{ ".htm", ".html", ".jpeg", ".jpg", ".png", ".gif" };

        directory_iterator end_itr;

        for( directory_iterator itr( dir ); itr != end_itr; ++itr ){
            if( is_regular_file( itr->path() ) ){
                set<string>::const_iterator is_ok = file_to_blob_filter.end();
                is_ok = file_to_blob_filter.find( itr->path().extension().string() );
                if( is_ok != file_to_blob_filter.end() ){
                    string file = itr->path().string();
                    cout << file << endl;
                    result.insert( itr->path() );
                }
            }
            else if( is_directory( itr->path() ) ){
                list_files( itr->path() );
            }
        }

        return result;
    }

    static void find_regex( const std::string& file, std::string& topic ){
        boost::regex re( "name=\\\"keywords\\\" content=\\\"#([\\w]*)\\\"" );
        std::ifstream iss( file );
        std::string line;
        boost::smatch what;
        while( std::getline( iss, line ) ){
            if( boost::regex_search( line, what, re ) ){
                std::cout << "ZNALAZLEM TOPIC !!!!" << endl;
                std::cout << what[ 1 ].str() << endl;
                topic = what[ 1 ].str();
            }
        }
    }

    static vector<Content> list_to_compile( const path& root, const set<path>& files ){
        vector<Content> result;

        for( auto file : files ){
            Content tmp;
            tmp.path = file;
            tmp.url = "/" + relative( tmp.path, root ).generic_string();
            
            if( file.extension() == ".png" ){
                tmp.type = ContentType::Image_png;
            }
            else if( file.extension() == ".jpg" ||
                     file.extension() == ".jpeg" ){
                tmp.type = ContentType::Image_jpg;
            }
            else if( file.extension() == ".gif" ){
                tmp.type = ContentType::Image_gif;
            }
            else if( file.extension() == ".html" ||
                     file.extension() == ".htm" ){
                std::string topic = "None";
                find_regex( file.string(), topic );
                tmp.type = ContentType::Page;
                tmp.topic = topic;
            }
            else{
                tmp.type = ContentType::None;
            }

            result.push_back( tmp );
        }

        return result;
    }
};

class ExampleHandler: public CivetHandler
{
public:
    bool handleGet( CivetServer* server, struct mg_connection* conn )
    {
        std::vector<std::string> pages;
        DbOracle::getInstance().get_all_urls( ContentType::Page, pages );

        mg_printf( conn, Tools::header_txt() );
        mg_printf( conn, "<html><body>\r\n" );

        for( auto& page : pages ){
            std::string entry = "<p>" + page + ": <a "
                "href=\"" + page.c_str() + "\">link</a></p>\r\n";
            mg_printf( conn, entry.c_str() );
        }

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

class ContentHandler: public CivetHandler
{
public:
    bool handleGet( CivetServer* server, struct mg_connection* conn )
    {
        const struct mg_request_info* req_info = mg_get_request_info( conn );
        std::cout << "URL: " << req_info->request_uri << std::endl;
       
        std::vector<char> content;
        int size = 0;
        ContentType type = ContentType::None;
        DbOracle::getInstance().get_content( req_info->request_uri, content, size, type);
        
        if( type == ContentType::Image_jpg ){
            mg_printf( conn, Tools::header_image_jpeg() );
            mg_write( conn, reinterpret_cast< char* >( content.data() ), size );
        }
        else if( type == ContentType::Image_png ){
            mg_printf( conn, Tools::header_image_png() );
            mg_write( conn, reinterpret_cast< char* >( content.data() ), size );
        }
        else if (type == ContentType::Page) {
            mg_printf( conn, Tools::header_txt() );
            mg_printf( conn, reinterpret_cast< char* >( &content[ 0 ] ) );
        }
 
        return true;
    }
};

class HelpHandler: public CivetHandler
{
    bool handleGet( CivetServer* server, struct mg_connection* conn ){
        const struct mg_request_info* req_info = mg_get_request_info( conn );
        std::cout << "Topic: " << req_info->request_uri << std::endl;
        std::string topic_tmp = req_info->request_uri;
        topic_tmp.erase( topic_tmp.begin(), topic_tmp.begin() + 7 );

        std::vector<std::string> urls;
        DbOracle::getInstance().get_content_by_topic( topic_tmp, urls );

        std::string page;
        Tools::prepare_link_page( topic_tmp, urls, page );
        mg_printf( conn, page.c_str() );

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
    Configuration config;
    ReadDbConfig( config );

    const char* options[] = {
        "document_root", DOCUMENT_ROOT, "listening_ports", PORT, 0 };

    vector<string> cpp_options;
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
    server.addHandler( "/topic", h_help );

    ContentHandler h_content;
    server.addHandler( "**", h_content );


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
    
    path help_dir( "c:\\Jopa\\HOME\\Projekty\\sentinel\\help" );
    path help_dir2( "c:\\Projekty\\trunk\\src\\rvs_help\\help2" );
    auto files = Tools::list_files( help_dir2 );
    auto content = Tools::list_to_compile( help_dir2, files );
    
    //Oracle
    DbOracle::getInstance().open_db( config.connectionString );
    
    DbOracle::getInstance().create_tables();
    for( auto item : content ){
        cout << "£adowanie do bazy: " << endl;
        cout << "url: " << item.url << endl;
        cout << "file: " << item.path << endl;
        cout << "type: " << static_cast< int >( item.type ) << endl;
        cout << "topic" << item.topic << endl;

        DbOracle::getInstance().put_content( item.url, static_cast< int >( item.type ),
                                             item.topic, item.path.string() );
    }
    

    //SQLite3
    /*
    DbSqlite3::getInstance().open_db( "jajo" );
    DbSqlite3::getInstance().create_tables();

    for( auto item : content ){
        cout << "£adowanie do bazy: " << endl;
        cout << "url: " << item.url << endl;
        cout << "file: " << item.path << endl;
        cout << "type: " << static_cast< int >( item.type ) << endl;

        DbSqlite3::getInstance().put_content( item.url, static_cast< int >( item.type ), 
                                              item.path.string() );
    }
    */
    while ( !exitNow ) {
        this_thread::sleep_for( chrono::seconds( 1 ) );
    }

    DbSqlite3::getInstance().close_db();
    DbOracle::getInstance().close_db();
    std::cout << "Bye!" << std::endl;

    return 0;
}
