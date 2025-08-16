// Minimal desktop CLI to exercise SSH forwarding and udp2tcp using shared sources.
// Usage examples:
//  1) Password SSH ping:  --ssh host user pass 22
//  2) Start forward only: --forward remote_host remote_port listen_port --ssh host user pass 22
//  3) Start udp2tcp: --udp2tcp rhost rport lhost lport ludp_host ludp_port rdst_host rdst_port --ssh host user pass 22

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>
#include "ssh_tunnel_api.h"
#include "udp2tcp_client_adapter.h"

static void usage(const char* prog) {
  printf("Usage:\n");
  printf("  %s --ssh <host> <user> <pass> <port>\n", prog);
  printf("  %s --forward <remote_host> <remote_port> <listen_port> [--forward-hold <sec>] --ssh <host> <user> <pass> <port>\n", prog);
  printf("  %s --udp2tcp <rhost> <rport> <lhost> <lport> <ludp_host> <ludp_port> <rdst_host> <rdst_port> [--stats-wait <sec>] [--stats-interval <sec>] --ssh <host> <user> <pass> <port>\n", prog);
  printf("Options:\n");
  printf("  --forward-hold <sec>        Hold forward for <sec> seconds (0 = infinite)\n");
  printf("  --udp2tcp-log-level <lvl>   Set udp2tcp log level: debug|info|warn|error (default: info)\n");
  printf("  --stats-wait <sec>          Total duration to run udp2tcp client before exit (default: 5; 0 = infinite until Ctrl+C)\n");
  printf("  --stats-interval <sec>      Print stats every <sec> seconds during run (default: disabled)\n");
}

// Simple local forward, re-implemented here for CLI: we will connect a TCP socket via SSH direct-tcpip when a local client arrives.
// For testing purposes, we only check the SSH connection and open/close a forward channel once.
static int test_open_forward_hold(const char* rhost, int rport, int listen_port, int hold_sec) {
  int rc = ssht_cli_start_port_forward(rhost, rport, listen_port);
  if (rc != 0) return rc;
  if (hold_sec <= 0) {
    // Hold indefinitely until killed (Ctrl+C)
    while (1) { sleep(1); }
  } else {
    sleep(hold_sec);
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 2) { usage(argv[0]); return 1; }
  std::vector<std::string> args(argv + 1, argv + argc);
  bool do_forward = false, do_udp2tcp = false, do_ssh = false;
  const char *ssh_host=nullptr, *ssh_user=nullptr, *ssh_pass=nullptr; int ssh_port=22;
  const char *f_rhost=nullptr; int f_rport=0, f_lport=0; int f_hold=1;
  const char *u_rhost=nullptr, *u_lhost=nullptr, *u_ludp_host=nullptr, *u_rdst_host=nullptr; int u_rport=0, u_lport=0, u_ludp_port=0, u_rdst_port=0;
  const char *u_loglevel=nullptr;
  int u_stats_wait=5; // seconds; 0 = infinite
  int u_stats_interval=0; // seconds; 0 = disabled

  for (size_t i=0; i<args.size();) {
    if (args[i] == "--ssh" && i+4 < args.size()) {
      ssh_host = args[i+1].c_str(); ssh_user = args[i+2].c_str(); ssh_pass = args[i+3].c_str(); ssh_port = std::stoi(args[i+4]);
      do_ssh = true; i += 5; continue;
    } else if (args[i] == "--forward" && i+3 < args.size()) {
      f_rhost = args[i+1].c_str(); f_rport = std::stoi(args[i+2]); f_lport = std::stoi(args[i+3]); do_forward = true; i += 4; continue;
    } else if (args[i] == "--udp2tcp" && i+8 < args.size()) {
      u_rhost=args[i+1].c_str(); u_rport=std::stoi(args[i+2]); u_lhost=args[i+3].c_str(); u_lport=std::stoi(args[i+4]);
      u_ludp_host=args[i+5].c_str(); u_ludp_port=std::stoi(args[i+6]); u_rdst_host=args[i+7].c_str(); u_rdst_port=std::stoi(args[i+8]);
      do_udp2tcp = true; i += 9; continue;
    } else if (args[i] == "--forward-hold" && i+1 < args.size()) {
      f_hold = std::stoi(args[i+1]); i += 2; continue;
    } else if (args[i] == "--udp2tcp-log-level" && i+1 < args.size()) {
      u_loglevel = args[i+1].c_str(); i += 2; continue;
    } else if (args[i] == "--stats-wait" && i+1 < args.size()) {
      u_stats_wait = std::stoi(args[i+1]); i += 2; continue;
    } else if (args[i] == "--stats-interval" && i+1 < args.size()) {
      u_stats_interval = std::stoi(args[i+1]); i += 2; continue;
    } else if (args[i] == "-h" || args[i] == "--help") {
      usage(argv[0]); return 0;
    } else {
      fprintf(stderr, "Unknown or incomplete arguments near '%s'\n", args[i].c_str());
      usage(argv[0]); return 2;
    }
  }

  if (!do_ssh) {
    fprintf(stderr, "--ssh is required\n");
    usage(argv[0]);
    return 2;
  }

  // Connect SSH via shared core
  int crc = ssht_cli_connect_password(ssh_host, ssh_port, ssh_user, ssh_pass);
  if (crc != 0) { fprintf(stderr, "ssh connect/auth failed, rc=%d\n", crc); return 4; }
  printf("SSH connected: %s@%s:%d\n", ssh_user, ssh_host, ssh_port);

  int rc = 0;
  if (do_forward) {
    rc = test_open_forward_hold(f_rhost ? f_rhost : "127.0.0.1", f_rport, f_lport, f_hold);
    printf("forward open result: %d\n", rc);
  }

  if (do_udp2tcp) {
    if (u_loglevel && *u_loglevel) { udp2tcp_set_log_level(u_loglevel); }
    // Expect that a real SSH forward is configured externally (or through --forward). Here we directly start the client to connect to local TCP.
    rc = udp2tcp_start(u_rhost ? u_rhost : "127.0.0.1", u_rport>0?u_rport:u_lport,
                       u_lhost ? u_lhost : "127.0.0.1", u_lport,
                       u_ludp_host ? u_ludp_host : "127.0.0.1", u_ludp_port,
                       u_rdst_host ? u_rdst_host : "127.0.0.1", u_rdst_port);
    printf("udp2tcp_start rc=%d\n", rc);
    if (rc==0) {
      // Periodic or single-shot stats printing
      if (u_stats_interval > 0) {
        if (u_stats_wait <= 0) {
          fprintf(stdout, "udp2tcp running... press Ctrl+C to stop (interval %ds)\n", u_stats_interval);
          fflush(stdout);
          while (true) {
            sleep(u_stats_interval);
            uint64_t txf=0,rxf=0,txb=0,rxb=0; udp2tcp_get_library_stats(&txf,&rxf,&txb,&rxb);
            printf("stats: TX frames=%llu bytes=%llu, RX frames=%llu bytes=%llu\n",
                   (unsigned long long)txf,(unsigned long long)txb,(unsigned long long)rxf,(unsigned long long)rxb);
            fflush(stdout);
          }
        } else {
          int elapsed = 0;
          fprintf(stdout, "udp2tcp running for %ds (interval %ds) ...\n", u_stats_wait, u_stats_interval);
          fflush(stdout);
          while (elapsed < u_stats_wait) {
            sleep(u_stats_interval);
            elapsed += u_stats_interval;
            uint64_t txf=0,rxf=0,txb=0,rxb=0; udp2tcp_get_library_stats(&txf,&rxf,&txb,&rxb);
            printf("stats: TX frames=%llu bytes=%llu, RX frames=%llu bytes=%llu\n",
                   (unsigned long long)txf,(unsigned long long)txb,(unsigned long long)rxf,(unsigned long long)rxb);
            fflush(stdout);
          }
        }
      } else {
        if (u_stats_wait <= 0) {
          fprintf(stdout, "udp2tcp running... press Ctrl+C to stop\n");
          fflush(stdout);
          while (true) { sleep(1); }
        } else {
          fprintf(stdout, "udp2tcp running... will print stats after %ds\n", u_stats_wait);
          fflush(stdout);
          for (int i=0;i<u_stats_wait;i++) { sleep(1); }
        }
        uint64_t txf=0,rxf=0,txb=0,rxb=0; udp2tcp_get_library_stats(&txf,&rxf,&txb,&rxb);
        printf("stats: TX frames=%llu bytes=%llu, RX frames=%llu bytes=%llu\n",
               (unsigned long long)txf,(unsigned long long)txb,(unsigned long long)rxf,(unsigned long long)rxb);
      }
      // If we reached here, either finite wait completed or we are being terminated externally
      if (u_stats_wait > 0) {
        udp2tcp_stop();
        udp2tcp_cleanup();
      }
    }
  }

  ssht_cli_disconnect();
  return rc;
}
