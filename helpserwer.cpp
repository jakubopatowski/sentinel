#include "CivetServer.h"

class HelpHandler: public CivetHandler
{
private:   
   bool handleAll(const char * method, CivetServer *server, struct mg_connection *conn) 
   {           
	   std::string param;
   
   if (CivetServer::getParam(conn, "page", param))
   {
      QString strona = QString::fromUtf8(param.c_str());
      if (CivetServer::getParam(conn, "remove_configuration", param))
      {
         char * pEnd;
         remove_configuration = strtol(param.c_str(), &pEnd, 10)==1;  
      }
      if (CivetServer::getParam(conn, "remove_all", param))
      {
         char * pEnd;
         remove_all = strtol(param.c_str(), &pEnd, 10)==1;         
      }
      if (ipaddress.isEmpty()||(!remove_all && !remove_configuration))
      {
         mg_printf(conn, "HTTP/1.1 400 Bad request\r\n\r\n");
         return true;
      }

      /*if (method=="DELETE")
      {*/
         dm_devinfo_t dev = getDevice(devices, ipaddress);
         if (dev.id == -1)
         {        
            dev = getDeviceForIP(devices,ipaddress);                     
         }
         if ( dev.id == -1 || dev.status == DeviceStatus::Deleted || dev.status == DeviceStatus::Rejected)
         {
            // b��d 
            mg_printf(conn, "HTTP/1.1 404 Not found\r\n\r\n");
            return true;
         }

         // test zadan czy jest cokolwiek do czyszczenia
      //}
      if (remove_configuration)
      {
         bool anyJobToCancel = false;
         for (std::set<r_int>::iterator int_jobIt=dev.jobs.begin(); int_jobIt!=dev.jobs.end(); ++int_jobIt)
         {     
            std::map<r_int,dm_job_t>::iterator jobIt = jobs.find(*int_jobIt);
            if (jobIt != jobs.end())
            {  
               dm_job_t job_pom = jobIt->second;                     
               if (job_pom.status == JobStatus::New || job_pom.status == JobStatus::InProgres || job_pom.status == JobStatus::WaitingForTransfer || job_pom.status == JobStatus::TransferInProgress || job_pom.status == JobStatus::TransferFinished)
               {
                  anyJobToCancel = true;
                  break;
               }
            }
         }
         if (!anyJobToCancel)
         {
            mg_printf(conn, "HTTP/1.1 404 Not found\r\n\r\n");
            return true;
         }
      }

      if (remove_configuration)
      {         
         dbThread->process_deactivateDev(ipaddress, false);
      }
      else if (remove_all)
      {       
         dbThread->process_deactivateDev(ipaddress, true);        
      }
   }
   mg_printf(conn, "HTTP/1.1 204 OK\r\n\r\n");
	return true;    
}
bool handleDel(const char * method, CivetServer *server, struct mg_connection *conn) 
{       

   //const struct mg_request_info *ri = mg_get_request_info(conn);
   QString ipaddress("");
   bool remove_configuration=false;
   bool remove_all = false;

   const char *_ipaddress = mg_get_header(conn, "ipaddress");
   if (_ipaddress) {
      ipaddress = QString(_ipaddress);
   }   
   
   const char *_remove_all = mg_get_header(conn, "remove_all");
   if (_remove_all) {
      remove_all = atoi(_remove_all)==1;
   }   

   const char *_remove_configuration = mg_get_header(conn, "remove_configuration");
   if (_remove_configuration) {
      remove_configuration = atoi(_remove_configuration)==1;
   }   

   if (ipaddress.isEmpty()||(!remove_all && !remove_configuration))
   {
      mg_printf(conn, "HTTP/1.1 400 Bad request\r\n\r\n");
      return true;
   }
   dm_devinfo_t dev = getDevice(devices, ipaddress);
   if (dev.id == -1)
   {        
      dev = getDeviceForIP(devices,ipaddress);                     
   }
   if ( dev.id == -1 || dev.status == DeviceStatus::Deleted || dev.status == DeviceStatus::Rejected)
   {
      // b��d 
      mg_printf(conn, "HTTP/1.1 404 Not found\r\n\r\n");
      return true;
   }

    
   if (remove_configuration)
   {
      bool anyJobToCancel = false;
      for (std::set<r_int>::iterator int_jobIt=dev.jobs.begin(); int_jobIt!=dev.jobs.end(); ++int_jobIt)
      {     
         std::map<r_int,dm_job_t>::iterator jobIt = jobs.find(*int_jobIt);
         if (jobIt != jobs.end())
         {  
            dm_job_t job_pom = jobIt->second;                     
            if (job_pom.status == JobStatus::New || job_pom.status == JobStatus::InProgres || job_pom.status == JobStatus::WaitingForTransfer || job_pom.status == JobStatus::TransferInProgress || job_pom.status == JobStatus::TransferFinished)
            {
               anyJobToCancel = true;
               break;
            }
         }
      }
      if (!anyJobToCancel)
      {
         mg_printf(conn, "HTTP/1.1 404 Not found\r\n\r\n");
         return true;
      }
   }

   if (remove_configuration)
   {         
      dbThread->process_deactivateDev(ipaddress, false);
   }
   else if (remove_all)
   {       
      dbThread->process_deactivateDev(ipaddress, true);        
   }
   
   mg_printf(conn, "HTTP/1.1 204 OK\r\n\r\n");
	return true;    
}
public:
    bool handleGet(CivetServer *server, struct mg_connection *conn) {
        return handleAll("GET", server, conn);
    }
    bool handlePost(CivetServer *server, struct mg_connection *conn) {
        return handleAll("POST", server, conn);
    }
    bool handleDelete(CivetServer *server, struct mg_connection *conn) {
        return handleDel("DELETE", server, conn);
    }
};

int main(int argc, char *argv[])
{
   unsigned char buffer[128];
   int rc = RAND_bytes(buffer, sizeof(buffer));
   ERR_get_error();
   if(rc != 1) {
      return 1;
   }

   RLog::start("rvs_dm.log","rvs_dmd.log",104857600);
   RLog::setup("rvs_dm");   
   
   if(RSystem::exePID("rvs_dm.exe").size()>1)
   {
        RLog::log("SYNDIS RV-DM server is allready running!");
        return 1;
   }

   memset(&callbacks1, 0, sizeof(SmlCallbacks_t));
   SetCallbacks(callbacks1);

   serverIP = opt.get("127.0.0.1", "externalAddress");
   port = opt.get("8081", "port");
   enable_directory_listing = opt.get("no","enable_directory_listing");
   enable_keep_alive = opt.get("yes","enable_keep_alive");

   RLog::log("Starting SYNDIS RV-DM server v. 3.21");
   RLog::log("External addres: http://%s  Open ports: %s",toLog(serverIP), toLog(port));
   if (!noSSLaddress.isEmpty())
      RLog::log("External no SSL addres: %s",toLog(noSSLaddress));

   std::string s_port = port.toStdString();
   std::string s_enable_directory_listing = enable_directory_listing.toStdString();
   std::string s_enable_keep_alive = enable_keep_alive.toStdString();
      const char * options[] = 
      { "document_root", s_archive_dir.c_str(),
        "listening_ports", s_port.c_str(),
        "access_log_file","dm_requests.log",
        "error_log_file","dm_errors.log",
        "num_threads","1500",
        "request_timeout_ms", s_request_timeout_ms.c_str(),
        "enable_directory_listing", s_enable_directory_listing.c_str(),
        "enable_keep_alive", s_enable_keep_alive.c_str(), 0};
  
  
   struct mg_callbacks callbacks;
   memset(&callbacks, 0, sizeof(callbacks));
   callbacks.upload = upload_handler;
   //callbacks.verify_callback = verify_callback;

   CivetServer server(options,&callbacks);   

   server.addHandler(EXIT_URI, new ExitHandler());
   server.addHandler("/help", new HelpHandler());
   server.addHandler("/quit", new QuitHandler());

    while (!exitNow) {
#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
    }
    printf("Bye!\n");
        
    return 0;
}

adres/help?page=Mapy
