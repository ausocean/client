#!/usr/bin/env bash
#
# esp-bridge.sh — Bridge internet connection to ethernet devices (e.g., ESP32 dev boards)
#                 via NAT + dnsmasq DHCP/DNS.
#
# Usage:
#   sudo ./esp-bridge.sh [options] {start|stop|status}
#
# Examples:
#   sudo ./esp-bridge.sh start
#   sudo ./esp-bridge.sh -l eth0 -w wlan0 start
#   LAN_IF=eth0 WAN_IF=wlan0 sudo ./esp-bridge.sh start

set -euo pipefail

# --- Colorized Formatting ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# --- Default Network Configurations ---
LAN_IP="${LAN_IP:-192.168.2.1}"
LAN_PREFIX="${LAN_PREFIX:-24}"
DHCP_RANGE_START="${DHCP_RANGE_START:-192.168.2.10}"
DHCP_RANGE_END="${DHCP_RANGE_END:-192.168.2.100}"
LEASE_TIME="${LEASE_TIME:-12h}"
DNSMASQ_PID="/run/esp-bridge-dnsmasq.pid"
DNSMASQ_LOG="/run/esp-bridge-dnsmasq.log"

# --- Interface Auto-Detection Functions ---
detect_wan_iface() {
  ip route show default 2>/dev/null | awk '/default/ {print $5; exit}'
}

detect_lan_iface() {
  local wan="${1:-}"
  # Pick the first non-loopback, non-wireless, non-WAN up/down interface
  for iface in $(ip -o link show | awk -F': ' '{print $2}'); do
    if [[ "$iface" != "lo" && "$iface" != "$wan" && "$iface" != docker* && "$iface" != veth* && "$iface" != br-* ]]; then
      echo "$iface"
      return 0
    fi
  done
  return 1
}

# --- Help & Usage ---
usage() {
  cat <<EOF
Usage: sudo $0 [options] {start|stop|status}

Options:
  -l, --lan-if IFACE   LAN interface connected to ESP32 (default: auto-detect)
  -w, --wan-if IFACE   WAN interface with internet access (default: auto-detect)
  -h, --help           Show this help message

Environment Variables:
  LAN_IF, WAN_IF, LAN_IP, DHCP_RANGE_START, DHCP_RANGE_END

EOF
  exit 0
}

# --- CLI Parameter Parsing ---
LAN_IF_ARG=""
WAN_IF_ARG=""

POSITIONAL_ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    -l|--lan-if)
      LAN_IF_ARG="$2"
      shift 2
      ;;
    -w|--wan-if)
      WAN_IF_ARG="$2"
      shift 2
      ;;
    -h|--help)
      usage
      ;;
    *)
      POSITIONAL_ARGS+=("$1")
      shift
      ;;
  esac
done

set -- "${POSITIONAL_ARGS[@]:-}"
COMMAND="${1:-}"

# Resolve Interface Variables
WAN_IF="${WAN_IF_ARG:-${WAN_IF:-$(detect_wan_iface)}}"
LAN_IF="${LAN_IF_ARG:-${LAN_IF:-$(detect_lan_iface "${WAN_IF}")}}"

# --- Validation and Dependency Checks ---
require_root() {
  if [[ $EUID -ne 0 ]]; then
    echo -e "${RED}Error: This script requires root permissions. Please run with sudo.${NC}" >&2
    exit 1
  fi
}

check_dependencies() {
  local deps=("ip" "iptables" "dnsmasq" "sysctl")
  local missing=()

  for cmd in "${deps[@]}"; do
    if ! command -v "$cmd" &>/dev/null; then
      missing+=("$cmd")
    fi
  done

  if [[ ${#missing[@]} -gt 0 ]]; then
    echo -e "${RED}Error: Missing required commands: ${missing[*]}${NC}\n" >&2

    # Identify Distro and offer quick install guidance
    if [[ -f /etc/os-release ]]; then
      . /etc/os-release
      case "${ID:-}" in
        arch|manjaro|endeavouros)
          echo -e "Install missing tools on Arch Linux using:"
          echo -e "  ${YELLOW}sudo pacman -S iproute2 iptables dnsmasq procps-ng${NC}\n"
          ;;
        ubuntu|debian|mint|pop)
          echo -e "Install missing tools on Ubuntu/Debian/Mint using:"
          echo -e "  ${YELLOW}sudo apt update && sudo apt install iproute2 iptables dnsmasq procps${NC}\n"
          ;;
        fedora|rhel|centos)
          echo -e "Install missing tools on Fedora/RHEL using:"
          echo -e "  ${YELLOW}sudo dnf install iproute iptables dnsmasq procps-ng${NC}\n"
          ;;
        *)
          echo -e "Please install package dependencies (${missing[*]}) using your distribution's package manager.\n"
          ;;
      esac
    fi
    exit 1
  fi
}

iface_exists() {
  ip link show "$1" &>/dev/null
}

docker_user_chain_exists() {
  iptables -L DOCKER-USER -n &>/dev/null
}

add_iptables_rule() {
  local args=("$@")
  local table=()
  if [[ "${args[0]}" == "-t" ]]; then
    table=("${args[0]}" "${args[1]}")
    args=("${args[@]:2}")
  fi
  if ! iptables "${table[@]}" -C "${args[@]}" &>/dev/null; then
    iptables "${table[@]}" -A "${args[@]}"
  fi
}

ensure_docker_user_rule() {
  local args=("$@")
  if ! iptables -C DOCKER-USER "${args[@]}" &>/dev/null; then
    iptables -I DOCKER-USER "${args[@]}"
  fi
}

validate_interfaces() {
  if [[ -z "$WAN_IF" ]]; then
    echo -e "${RED}Error: Could not auto-detect WAN interface. Specify using -w <interface>${NC}" >&2
    exit 1
  fi
  if [[ -z "$LAN_IF" ]]; then
    echo -e "${RED}Error: Could not auto-detect LAN interface. Specify using -l <interface>${NC}" >&2
    exit 1
  fi
  for ifc in "$LAN_IF" "$WAN_IF"; do
    iface_exists "$ifc" || { echo -e "${RED}Error: Interface '$ifc' not found on system.${NC}" >&2; exit 1; }
  done
}

# --- Core Commands ---
start() {
  require_root
  check_dependencies
  validate_interfaces

  echo -e "${BLUE}==> Bridging Internet:${NC} $WAN_IF ---> $LAN_IF ($LAN_IP)"

  echo "==> Enabling IP forwarding"
  sysctl -w net.ipv4.ip_forward=1 >/dev/null

  echo "==> Bringing up $LAN_IF with $LAN_IP/$LAN_PREFIX"
  ip link set "$LAN_IF" up
  if ! ip addr show "$LAN_IF" | grep -q "$LAN_IP/$LAN_PREFIX"; then
    ip addr add "$LAN_IP/$LAN_PREFIX" dev "$LAN_IF"
  fi

  echo "==> Configuring NAT (masquerade out $WAN_IF)"
  add_iptables_rule -t nat POSTROUTING -o "$WAN_IF" -j MASQUERADE
  add_iptables_rule FORWARD -i "$LAN_IF" -o "$WAN_IF" -j ACCEPT
  add_iptables_rule FORWARD -i "$WAN_IF" -o "$LAN_IF" -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT

  if docker_user_chain_exists; then
    echo "==> Docker detected — adding matching rules to DOCKER-USER"
    ensure_docker_user_rule -i "$LAN_IF" -o "$WAN_IF" -j ACCEPT
    ensure_docker_user_rule -i "$WAN_IF" -o "$LAN_IF" -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
  fi

  echo "==> Starting dnsmasq for DHCP + DNS on $LAN_IF"
  if [[ -f "$DNSMASQ_PID" ]] && kill -0 "$(cat "$DNSMASQ_PID")" 2>/dev/null; then
    echo "    dnsmasq already running (pid $(cat "$DNSMASQ_PID"))"
  else
    chattr -i "$DNSMASQ_PID" "$DNSMASQ_LOG" 2>/dev/null || true
    rm -f "$DNSMASQ_PID" "$DNSMASQ_LOG"

    dnsmasq \
      --interface="$LAN_IF" \
      --bind-interfaces \
      --dhcp-range="$DHCP_RANGE_START,$DHCP_RANGE_END,255.255.255.0,$LEASE_TIME" \
      --dhcp-option="option:router,$LAN_IP" \
      --dhcp-option="option:dns-server,$LAN_IP" \
      --no-daemon \
      >>"$DNSMASQ_LOG" 2>&1 &
    echo $! > "$DNSMASQ_PID"
    echo "    dnsmasq started (pid $!, log: $DNSMASQ_LOG)"
  fi

  echo -e "${GREEN}==> Success! Devices on $LAN_IF can now request DHCP and access internet.${NC}"
}

stop() {
  require_root
  check_dependencies
  validate_interfaces

  echo -e "${BLUE}==> Stopping bridge on $LAN_IF / $WAN_IF${NC}"

  echo "==> Stopping dnsmasq"
  if [[ -f "$DNSMASQ_PID" ]] && kill -0 "$(cat "$DNSMASQ_PID")" 2>/dev/null; then
    kill "$(cat "$DNSMASQ_PID")"
    rm -f "$DNSMASQ_PID"
  else
    pkill -f "dnsmasq.*$LAN_IF" 2>/dev/null || true
  fi

  echo "==> Removing iptables rules"
  iptables -t nat -D POSTROUTING -o "$WAN_IF" -j MASQUERADE 2>/dev/null || true
  iptables -D FORWARD -i "$LAN_IF" -o "$WAN_IF" -j ACCEPT 2>/dev/null || true
  iptables -D FORWARD -i "$WAN_IF" -o "$LAN_IF" -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || true
  if docker_user_chain_exists; then
    iptables -D DOCKER-USER -i "$LAN_IF" -o "$WAN_IF" -j ACCEPT 2>/dev/null || true
    iptables -D DOCKER-USER -i "$WAN_IF" -o "$LAN_IF" -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || true
  fi

  echo "==> Removing $LAN_IP from $LAN_IF"
  ip addr del "$LAN_IP/$LAN_PREFIX" dev "$LAN_IF" 2>/dev/null || true

  echo -e "${GREEN}==> Done dismantling bridge.${NC}"
}

status() {
  check_dependencies
  validate_interfaces

  echo -e "${BLUE}--- Active Configuration ---${NC}"
  echo "WAN Interface : $WAN_IF"
  echo "LAN Interface : $LAN_IF"
  echo ""
  echo -e "${BLUE}--- IP Forwarding Status ---${NC}"
  sysctl net.ipv4.ip_forward
  echo ""
  echo -e "${BLUE}--- Interface Address ($LAN_IF) ---${NC}"
  ip addr show "$LAN_IF" | grep inet || echo "none"
  echo ""
  echo -e "${BLUE}--- Service Status (dnsmasq) ---${NC}"
  if [[ -f "$DNSMASQ_PID" ]] && kill -0 "$(cat "$DNSMASQ_PID")" 2>/dev/null; then
    echo -e "${GREEN}running${NC} (pid $(cat "$DNSMASQ_PID"))"
  else
    echo -e "${RED}not running${NC}"
  fi
  echo ""
  echo -e "${BLUE}--- Relevant iptables Rules ---${NC}"
  iptables -t nat -L POSTROUTING -v -n | grep "$WAN_IF" || echo "no NAT rule"
  iptables -L FORWARD -v -n | grep -E "$LAN_IF|$WAN_IF" || echo "no FORWARD rules"
  if docker_user_chain_exists; then
    echo -e "\n${BLUE}--- DOCKER-USER Chain Rules ---${NC}"
    iptables -L DOCKER-USER -v -n | grep -E "$LAN_IF|$WAN_IF" || echo "no DOCKER-USER rules"
  fi
}

case "${COMMAND}" in
  start)  start ;;
  stop)   stop ;;
  status) status ;;
  *)
    echo -e "${RED}Error: Invalid action '${COMMAND}'${NC}" >&2
    usage
    ;;
esac
