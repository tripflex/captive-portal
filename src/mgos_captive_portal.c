/*
 * Copyright (c) 2019 Myles McNamara
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the ""License"");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an ""AS IS"" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdlib.h>
#include <string.h>

#include "mgos_utils.h"
#include "mgos_config.h"
#include "mgos_mongoose.h"
#include "mgos_captive_portal.h"
#include "mongoose.h"

#if CS_PLATFORM == CS_P_ESP8266
#include "user_interface.h"
#endif

static const char *s_ap_ip = "192.168.4.1";
static const char *s_portal_hostname = "setup.device.portal";
static const char *s_listening_addr = "udp://:53";
static const char *s_portal_index_file = "index.html";
static const char *s_portal_redirect_file = "";

static int s_captive_portal_init = 0;

static struct mg_serve_http_opts s_http_server_opts;

static void http_msg_print(const struct http_message *msg){
    //LOG(LL_DEBUG, ("\n\n   MESSAGE: \n \"%.*s\"\n\n", msg->message.len, msg->message.p));
    LOG(LL_DEBUG, ("      method: \"%.*s\"", msg->method.len, msg->method.p));
    LOG(LL_DEBUG, ("         uri: \"%.*s\"", msg->uri.len, msg->uri.p));
}

// static bool x_requested_with_android( struct http_message *msg ){
    
//     struct mg_str *hdrval = mg_get_http_header(msg, "X-Requested-With");

//     if (hdrval != NULL && strstr(hdrval->p, "com.android.captiveportallogin") != NULL ){
//         return true;
//     }

//     return false;
// }

static bool accept_gzip_encoding( struct http_message *msg ){
    
    struct mg_str *hdrval = mg_get_http_header(msg, "Accept-Encoding");

    if (hdrval != NULL && strstr(hdrval->p, "gzip") != NULL ){
        return true;
    }

    return false;
}

static bool user_agent_captivenetworksupport( struct http_message *msg ){
    
    struct mg_str *hdrval = mg_get_http_header(msg, "User-Agent");
    
    LOG(LL_DEBUG, ("Captive Portal -- Checking for CaptivePortal UserAgent"));

    if (hdrval != NULL && strstr(hdrval->p, "CaptiveNetworkSupport") != NULL ){
        return true;
    }

    return false;
}

static bool gzip_file_requested(struct http_message *msg){
    struct mg_str uri = mg_mk_str_n(msg->uri.p, msg->uri.len);
    return strncmp(uri.p + uri.len - 3, ".gz", 3) == 0;
}

static bool is_captive_portal_hostname(struct http_message *msg){
    struct mg_str *hhdr = mg_get_http_header(msg, "Host");
    bool matches = hhdr != NULL && strstr(hhdr->p, s_portal_hostname) != NULL;
    if( ! matches ){
        LOG(LL_DEBUG, ("Root Handler -- HostName Not Match Portal - Actual: %s ", hhdr->p ));
    }
    return matches;
}

static bool is_device_ip_hostname(struct http_message *msg){
    struct mg_str *hhdr = mg_get_http_header(msg, "Host");
    bool matches = hhdr != NULL && strstr(hhdr->p, s_ap_ip) != NULL;
    if( ! matches ){
        LOG(LL_DEBUG, ("Root Handler -- HostName Does NOT Match Device IP Actual: %s ", hhdr->p ));
    }
    return matches;
}

static bool ends_in_gz( const char *string ){

    if(strlen(string) > 3 && !strcmp(string + strlen(string) - 3, ".gz")){
        return true;
    }

    return false;
}

char *get_redirect_url(void){
    static char redirect_url[256];
    // Set URI as HTTPS if ssl cert configured, otherwise use http
    c_snprintf(redirect_url, sizeof redirect_url, "%s://%s", (mgos_sys_config_get_http_ssl_cert() ? "https" : "http"), s_portal_hostname);
    return redirect_url;
}

static void serve_captive_portal_file(const char *file, struct mg_connection *nc, struct http_message *msg){

    if( ends_in_gz( file ) ){

        if( accept_gzip_encoding(msg) ){
            LOG(LL_DEBUG, ("-- Captive Portal Serving GZIP HTML file %s \n", file ));
            mg_http_serve_file(nc, msg, file, mg_mk_str("text/html"), mg_mk_str("Access-Control-Allow-Origin: *\r\nContent-Encoding: gzip"));
            return;
        }

        LOG(LL_DEBUG, ("-- Captive Portal Client DOES NOT SUPPORT GZIP -- Serving no_gzip.html File!\n" ));
        mg_http_serve_file(nc, msg, "no_gzip.html", mg_mk_str("text/html"), mg_mk_str("Access-Control-Allow-Origin: *"));
        return;
    }
    
    LOG(LL_DEBUG, ("-- Captive Portal Serving HTML file %s \n", file ));
    mg_http_serve_file(nc, msg, file, mg_mk_str("text/html"), mg_mk_str("Access-Control-Allow-Origin: *"));
}

static void serve_captive_portal_index_file(struct mg_connection *nc, int ev, void *p, void *user_data){
    if (ev != MG_EV_HTTP_REQUEST)
        return;

    struct http_message *msg = (struct http_message *)(p);
    serve_captive_portal_file( s_portal_index_file, nc, msg );

    (void)ev;
    (void)user_data;
}

// static void send_redirect_html_generated2(struct mg_connection *nc, int status_code,
//                            const struct mg_str location,
//                            const struct mg_str extra_headers) {
//   char bbody[100], *pbody = bbody;
//   int bl = mg_asprintf(&pbody, sizeof(bbody),
//                        "<html><head><title>Redirecting to Captive Portal</title><meta http-equiv='refresh' content='0; url=%.*s'></head><body><p>Click <a href='%.*s'>here</a> to login.</p></body></html>\r\n",
//                        (int) location.len, location.p, (int) location.len, location.p );
//   char bhead[150], *phead = bhead;
//   mg_asprintf(&phead, sizeof(bhead),
//               "Location: %.*s\r\n"
//               "Content-Type: text/html\r\n"
//               "Content-Length: %d\r\n"
//               "Cache-Control: no-cache\r\n"
//               "%.*s%s",
//               (int) location.len, location.p, bl, (int) extra_headers.len,
//               extra_headers.p, (extra_headers.len > 0 ? "\r\n" : ""));
//   mg_send_response_line(nc, status_code, phead);
//   if (phead != bhead) free(phead);
//   mg_send(nc, pbody, bl);
//   if (pbody != bbody) free(pbody);
// }

static void send_redirect_html_generated(struct mg_connection *nc, int status_code, const struct mg_str location ) {
  char bbody[100], *pbody = bbody;
  int bl = mg_asprintf(&pbody, sizeof(bbody),
                       "<html><head><title>Redirecting to Captive Portal</title><meta http-equiv='refresh' content='0; url=%.*s'></head><body><p>Please wait, refreshing.  If page does not refresh, click <a href='%.*s'>here</a> to login.</p></body></html>\r\n",
                       (int) location.len, location.p, (int) location.len, location.p );
  mg_send_head(nc, status_code, bl, "Cache-Control: no-cache" );
  mg_send(nc, pbody, bl);
  if (pbody != bbody) free(pbody);
}

static void redirect_ev_handler(struct mg_connection *nc, int ev, void *p, void *user_data){

    if (ev != MG_EV_HTTP_REQUEST)
        return;

    char *redirect_url = get_redirect_url();
    LOG(LL_DEBUG, (" --====== Sending 302 Redirect to %s for Captive Portal  ======--", redirect_url ) );

    struct http_message *msg = (struct http_message *)(p);
    http_msg_print(msg);
    
    mg_http_send_redirect(nc, 302, mg_mk_str(redirect_url), mg_mk_str(NULL));

    // (void)p;
    (void)user_data;
}

static void serve_redirect_ev_handler(struct mg_connection *nc, int ev, void *p, void *user_data){

    if (ev != MG_EV_HTTP_REQUEST)
        return;

    char *redirect_url = get_redirect_url();
    LOG(LL_DEBUG, (" --====== Serving Redirect HTML to %s for Captive Portal ======--", redirect_url ) );

    struct http_message *msg = (struct http_message *)(p);
    http_msg_print(msg);

    if( (s_portal_redirect_file != NULL) && (s_portal_redirect_file[0] == '\0') ){
        serve_captive_portal_file( s_portal_redirect_file, nc, msg); 
    } else {
        send_redirect_html_generated( nc, 200, mg_mk_str(redirect_url) );
    }

    // (void)p;
    (void)user_data;
}

static void dns_ev_handler(struct mg_connection *c, int ev, void *ev_data,
                                    void *user_data){
    struct mg_dns_message *msg = (struct mg_dns_message *)ev_data;
    struct mbuf reply_buf;
    int i;

    if (ev != MG_DNS_MESSAGE)
        return;

    mbuf_init(&reply_buf, 512);
    struct mg_dns_reply reply = mg_dns_create_reply(&reply_buf, msg);
    for (i = 0; i < msg->num_questions; i++)
    {
        char rname[256];
        struct mg_dns_resource_record *rr = &msg->questions[i];
        mg_dns_uncompress_name(msg, &rr->name, rname, sizeof(rname) - 1);
        // LOG( LL_DEBUG, ( "Q type %d name %s\n", rr->rtype, rname ) );
        if (rr->rtype == MG_DNS_A_RECORD)
        {
            LOG(LL_DEBUG, ("DNS A Query for %s sending IP %s", rname, s_ap_ip));
            uint32_t ip = inet_addr(s_ap_ip);
            mg_dns_reply_record(&reply, rr, NULL, rr->rtype, 10, &ip, 4);
        }
    }
    mg_dns_send_reply(c, &reply);
    mbuf_free(&reply_buf);
    (void)user_data;
}

static void root_handler(struct mg_connection *nc, int ev, void *p, void *user_data){
    (void)user_data;
    if (ev != MG_EV_HTTP_REQUEST)
        return;

    struct http_message *msg = (struct http_message *)(p);
    http_msg_print(msg);

    // Init our http server options (set in mgos_captive_portal_start)
    struct mg_serve_http_opts opts;
    memcpy(&opts, &s_http_server_opts, sizeof(opts));

    struct mg_str uri = mg_mk_str_n(msg->uri.p, msg->uri.len);
    // Check if URI is root directory
    bool uriroot = strncmp(uri.p, "/ HTTP", 6) == 0;

    if ( is_captive_portal_hostname(msg) ){
        LOG(LL_DEBUG, ("Root Handler -- Host matches Captive Portal Host \n"));

        // If gzip file requested -- set Content-Encoding
        if (gzip_file_requested(msg) && accept_gzip_encoding(msg)){
            LOG(LL_DEBUG, ("Root Handler -- gzip Asset Requested -- Adding Content-Encoding Header \n"));
            opts.extra_headers = "Access-Control-Allow-Origin: *\r\nContent-Encoding: gzip";
        }

        opts.index_files = s_portal_index_file;

        if (uriroot){
            serve_captive_portal_file( s_portal_index_file, nc, msg);
            return;
        } else {
            LOG(LL_DEBUG, ("\n Captive Portal Host BUT NOT URI Root - Actual: %s - %d\n", uri.p, uriroot));
        }

    } else {

        // Check for CaptivePortal useragent and send redirect if found
        if( user_agent_captivenetworksupport(msg) ){
            LOG(LL_DEBUG, ("Root Handler -- Found USER AGENT CaptiveNetworkSupport -- Sending Redirect!\n"));
            redirect_ev_handler(nc, ev, p, user_data);
            return;
        }

        // Check if direct access to IP address
        if( mgos_sys_config_get_cportal_ip_redirect() && is_device_ip_hostname(msg) && uriroot ){
            LOG(LL_DEBUG, ("Root Handler -- Direct IP accessed -- Sending Redirect!\n"));
            redirect_ev_handler(nc, ev, p, user_data);
            return;
        }

        // Requested hostname does not match captive portal hostname, and user agent did not match either
        // If serve any enabled, serve portal index file regardless of requested hostname
        if ( mgos_sys_config_get_cportal_any() && uriroot ){
            LOG(LL_DEBUG, ("Captive Portal -- NOT Host -- Serve Any Enabled -- Serving Portal Index File!\n"));
            serve_captive_portal_file( s_portal_index_file, nc, msg );
            return;
        }
    }

    LOG(LL_DEBUG, (" --===== Root Handler -- SERVING DEFAULT NO MATCHES =====--- "));
    // Serve non-root requested file
    mg_serve_http(nc, msg, opts);
}

bool mgos_captive_portal_start(void){

    if ( s_captive_portal_init ){
        LOG(LL_ERROR, ("Captive portal already init! Ignoring call to start captive portal!"));
        return false;
    }

    LOG(LL_DEBUG, ("Starting Captive Portal..."));

    #if CS_PLATFORM == CS_P_ESP8266
    int on = 1;
    wifi_softap_set_dhcps_offer_option(OFFER_ROUTER, &on);
    #endif
    /*
     *    TODO:
     *    Maybe need to figure out way to handle DNS for captive portal, if user has defined AP hostname,
     *    as WiFi lib automatically sets up it's own DNS responder for the hostname when one is set
     */
    // if (mgos_sys_config_get_wifi_ap_enable() && mgos_sys_config_get_wifi_ap_hostname() != NULL) {
    // }

    // Set IP address to respond to DNS queries with
    s_ap_ip = mgos_sys_config_get_wifi_ap_ip();
    // Set Hostname used for serving DNS captive portal
    s_portal_hostname = mgos_sys_config_get_cportal_hostname();
    s_portal_index_file = mgos_sys_config_get_cportal_index();
    s_portal_redirect_file = mgos_sys_config_get_cportal_redirect_file();

    // Bind DNS for Captive Portal
    struct mg_connection *dns_c = mg_bind(mgos_get_mgr(), s_listening_addr, dns_ev_handler, 0);

    if (dns_c == NULL){
        // wifi.ap.hostname value should be empty
        LOG(LL_ERROR, ("Failed to initialize DNS listener, The port may already be in use."));
        return false;
    } else {
        mg_set_protocol_dns(dns_c);
        LOG(LL_DEBUG, ("Captive Portal DNS Listening on %s", s_listening_addr));
    }

    // GZIP handling
    memset(&s_http_server_opts, 0, sizeof(s_http_server_opts));
    // s_http_server_opts.document_root = mgos_sys_config_get_http_document_root();
    s_http_server_opts.document_root = "/";
    // Add GZIP mime types for HTML, JavaScript, and CSS files
    s_http_server_opts.custom_mime_types = ".html.gz=text/html; charset=utf-8,.js.gz=application/javascript; charset=utf-8,.css.gz=text/css; charset=utf-8";
    // CORS
    s_http_server_opts.extra_headers = "Access-Control-Allow-Origin: *";
    
    // s_http_server_opts.index_files = s_portal_index_file;

    mgos_register_http_endpoint("/", root_handler, NULL);

    // captive.apple.com - DNS request for Mac OSX
    
    // Known HTTP GET requests to check for Captive Portal
    mgos_register_http_endpoint("/mobile/status.php", serve_redirect_ev_handler, NULL);         // Android 8.0 (Samsung s9+)
    mgos_register_http_endpoint("/generate_204", serve_redirect_ev_handler, NULL);              // Android
    mgos_register_http_endpoint("/gen_204", redirect_ev_handler, NULL);                   // Android 9.0
    mgos_register_http_endpoint("/ncsi.txt", redirect_ev_handler, NULL);                  // Windows
    mgos_register_http_endpoint("/success.txt", redirect_ev_handler, NULL);       // OSX
    mgos_register_http_endpoint("/hotspot-detect.html", redirect_ev_handler, NULL);       // iOS 8/9
    mgos_register_http_endpoint("/hotspotdetect.html", redirect_ev_handler, NULL);       // iOS 8/9
    mgos_register_http_endpoint("/library/test/success.html", redirect_ev_handler, NULL); // iOS 8/9
    // Kindle when requested with com.android.captiveportallogin
    // To prevent warning saying "Insecure Connection" from redirect, we instead just immediately serve the captive portal file
    mgos_register_http_endpoint("/kindle-wifi/wifiredirect.html", serve_captive_portal_index_file, NULL);
    // Kindle before requesting with captive portal login window (maybe for detection?)
    mgos_register_http_endpoint("/kindle-wifi/wifistub.html", serve_captive_portal_index_file, NULL);

    s_captive_portal_init = true;

    return true;
}

bool mgos_captive_portal_init(void){
    if (mgos_sys_config_get_cportal_enable()){
        mgos_captive_portal_start();
    }
    return true;
}
