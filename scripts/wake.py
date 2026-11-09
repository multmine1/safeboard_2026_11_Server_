import json
import socket

from _common import GENERATED

if __name__ == "__main__":
    configuration = json.loads((GENERATED / "ansible/hypervisor-bootstrap.json").read_text())
    if "wake" not in configuration:
        raise SystemExit("Wake-on-LAN is disabled in the source configuration.")
    wake = configuration["wake"]
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
        connection.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        connection.sendto(bytes.fromhex(wake["packet_hex"]), (wake["address"], wake["port"]))
