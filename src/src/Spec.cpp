#include "Spec.h"
#include "llvm/ADT/StringRef.h"
#include <unordered_map>

#define STR_SET std::unordered_set<std::string>

namespace RefCntSpec {

std::unordered_map<std::string, std::string> IncDecPairs = {
    // linux kernel
    {"get_page", "put_page"},
    {"css_get", "css_put"},
    {"btrfs_log_get_delayed_items", "btrfs_log_put_delayed_items"},
    {"btrfs_get_caching_control", "btrfs_put_caching_control"},
    {"fdget", "fdput"},
    {"btrfs_get_block_group", "btrfs_put_block_group"},
    {"dget", "dput"},
    {"zstd_get_workspace", "zstd_put_workspace"},
    {"btrfs_get_tree_mod_seq", "btrfs_put_tree_mod_seq"},
    {"btrfs_get_dev_args_from_path", "btrfs_put_dev_args_from_path"},
    {"scrub_workers_get", "scrub_workers_put"},
    {"fget", "fput"},
    {"btrfs_readdir_get_delayed_items", "btrfs_readdir_put_delayed_items"},
    {"btrfs_get_bioc", "btrfs_put_bioc"},
    {"folio_get", "folio_put"},
    {"get_workspace", "put_workspace"},
    {"btrfs_get_workspace", "btrfs_put_workspace"},
    {"bio_list_get", "bio_list_put"},
    {"nilfs_get_root", "nilfs_put_root"},
    {"get_bh", "put_bh"},
    {"get_bh", "brelse"},
    {"cifs_get_writer", "cifs_put_writer"},
    {"acpi_ipmi_msg_get", "acpi_ipmi_msg_put"},
    {"acpi_get_table", "acpi_put_table"},
    {"acpi_tb_get_table", "acpi_tb_put_table"},
    {"pm_runtime_get_sync", "pm_runtime_put_sync"},
    {"acpi_video_bus_get_devices", "acpi_video_bus_put_devices"},
    {"acpi_dev_get", "acpi_dev_put"},
    {"acpi_nhlt_get_gbl_table", "acpi_nhlt_put_gbl_table"},
    {"acpi_get_acpi_dev", "acpi_put_acpi_dev"},
    {"get_page", "put_page"},
    {"folio_get", "folio_put"},
    {"lockref_get", "lockref_put"},
    {"cifs_get_tcon", "cifs_put_tcon"},
    {"kref_get", "kref_put"},
    {"path_get", "path_put"},
    {"opinfo_get", "opinfo_put"},
    {"cifs_get_smb_ses", "cifs_put_smb_ses"},
    {"cifs_get_swn_reg", "cifs_put_swn_reg"},
    {"cpufreq_cpu_get", "cpufreq_cpu_put"},
    {"kref_get", "kref_put"},
    {"fdget", "fdput"},
    // {"get_receive_buffer", "put_receive_buffer"},
    {"get_net", "put_net"},
    {"cifs_get_tcp_session", "cifs_put_tcp_session"},
    {"cifs_get_tlink", "cifs_put_tlink"},
    {"ksmbd_share_config_get", "ksmbd_share_config_put"},
    {"cifsFileInfo_get", "cifsFileInfo_put"},
    {"cifsFileInfo_get", "_cifsFileInfo_put"},
    {"get_task_struct", "put_task_struct"},
    {"ksmbd_inode_get", "ksmbd_inode_put"},
    // {"get_empty_recvmsg", "put_empty_recvmsg"},
    {"__cifs_get_super", "__cifs_put_super"},
    {"ksmbd_user_session_get", "ksmbd_user_session_put"},
    {"pm_runtime_get", "pm_runtime_put"},
    {"usb_autopm_get_interface", "usb_autopm_put_interface"},
    {"pm_runtime_get_sync", "pm_runtime_put_sync"},
    {"nvmem_cell_get", "nvmem_cell_put"},
    {"bt3c_get", "bt3c_put"},
    {"devm_gpiod_get", "devm_gpiod_put"},
    {"vhci_get_user", "vhci_put_user"},
    {"devm_clk_get", "devm_clk_put"},
    {"get_device", "put_device"},
    {"acpi_ipmi_dev_get", "acpi_ipmi_dev_put"},
    // hmos
    {"GetBnSession", "PutBnSession"},
    {"GetBnContext", "PutBnContext"},
    //
    {"batadv_gw_node_get", "batadv_gw_node_put"},
    {"batadv_neigh_ifinfo_get", "batadv_neigh_ifinfo_put"},
    {"batadv_orig_node_vlan_get", "batadv_orig_node_vlan_put"},
    {"batadv_orig_ifinfo_get", "batadv_orig_ifinfo_put"},
    {"batadv_meshif_vlan_get", "batadv_meshif_vlan_put"},
    {"batadv_tvlv_container_get", "batadv_tvlv_container_put"},
    {"batadv_neigh_node_get", "batadv_neigh_node_put"},
    {"kref_get", "kref_put"},
    {"batadv_tvlv_handler_get", "batadv_tvlv_handler_put"},
    {"batadv_hardif_neigh_get", "batadv_hardif_neigh_put"},
};
str_set IncFuncs = {"createObject", "createEmbeddedStringObject", "kvobjCreate",
                    "kvobjCreateEmbedString", "incrRefCount"};
str_set DecFuncs = {"decrRefCount"};

void init() {
  for (auto &p : IncDecPairs) {
    IncFuncs.insert(p.first);
    DecFuncs.insert(p.second);
  }
}

} // namespace RefCntSpec

bool match(llvm::StringRef FuncName, str_set &SpecFuncs) {
  for (const auto &F : SpecFuncs) {
    if (FuncName.startswith(F)) {
      return true;
    }
  }
  return false;
}