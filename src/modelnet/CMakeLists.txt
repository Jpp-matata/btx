# Copyright (c) 2026 The BTX developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

if(NOT WITH_MODELNET)
  return()
endif()

find_package(OpenSSL REQUIRED)

add_library(bitcoin_modelnet STATIC
  types.cpp
  crypto.cpp
  resource_uri.cpp
  records.cpp
  policy.cpp
  firstrun.cpp
  protocol.cpp
  store.cpp
  piece_store.cpp
  swarm.cpp
  swarm_observer.cpp
  swarm_codec.cpp
  swarm_lifecycle.cpp
  swarm_execution.cpp
  swarm_evolution.cpp
  swarm_economy.cpp
  swarm_channel.cpp
  swarm_membership.cpp
  swarm_records.cpp
  swarm_frontier.cpp
  swarm_paymesh.cpp
  swarm_close.cpp
  swarm_node.cpp
  swarm_clearing.cpp
  swarm_liquidity.cpp
  swarm_live.cpp
  swarm_funding_class.cpp
  swarm_timescale.cpp
  swarm_raftlog.cpp
  swarm_piece.cpp
  swarm_mandate.cpp
  swarm_attempt.cpp
  swarm_fabric.cpp
  swarm_follow.cpp
  swarm_opening.cpp
  swarm_appointment.cpp
  swarm_boundary.cpp
  swarm_incident.cpp
  swarm_seal.cpp
  swarm_cas.cpp
  swarm_executor.cpp
  swarm_guard.cpp
  swarm_access.cpp
  swarm_manifest.cpp
  swarm_graph.cpp
  swarm_tenant.cpp
  swarm_pressure.cpp
  swarm_quota.cpp
  swarm_taskfence.cpp
  swarm_cycle.cpp
  swarm_fault.cpp
  swarm_faultmore.cpp
  swarm_cohortbook.cpp
  swarm_verify.cpp
  swarm_rollout.cpp
  swarm_engine.cpp
  swarm_island.cpp
  swarm_invoice.cpp
  swarm_propose.cpp
  swarm_regress.cpp
  swarm_dispute.cpp
  swarm_revokegate.cpp
  swarm_gradleak.cpp
  swarm_holdout.cpp
  swarm_scaleexec.cpp
  swarm_meter.cpp
  swarm_trajectory.cpp
  swarm_agentproto.cpp
  swarm_sandbox.cpp
  swarm_directory.cpp
  swarm_bench.cpp
  swarm_specbranch.cpp
  swarm_fundscript.cpp
  swarm_kvadapter.cpp
  swarm_reorg.cpp
  swarm_runtimepick.cpp
  swarm_toolcont.cpp
  swarm_toolreuse.cpp
  swarm_confine.cpp
  swarm_opportunity.cpp
  swarm_distill.cpp
  swarm_lora.cpp
  swarm_routecheck.cpp
  swarm_sybil.cpp
  swarm_tokfp.cpp
  swarm_reward.cpp
  swarm_fsjail.cpp
  swarm_catalogevt.cpp
  swarm_enckv.cpp
  swarm_receipt.cpp
  swarm_unsettle.cpp
  swarm_ckptpub.cpp
  swarm_refundhold.cpp
  swarm_ownergone.cpp
  swarm_promohalt.cpp
  swarm_partial.cpp
  swarm_canaryhold.cpp
  swarm_truncpage.cpp
  swarm_survivor.cpp
  swarm_workerdrop.cpp
  swarm_forknote.cpp
  swarm_evalsplit.cpp
  swarm_staleepoch.cpp
  swarm_domaingap.cpp
  swarm_replaystall.cpp
  swarm_untaxed.cpp
  swarm_visiblehead.cpp
  swarm_dlgrant.cpp
  swarm_originnote.cpp
  swarm_profit.cpp
  swarm_masshold.cpp
  swarm_spendgate.cpp
  swarm_gradrealm.cpp
  swarm_selfeval.cpp
  swarm_niche.cpp
  swarm_unfunded.cpp
  swarm_doubleuse.cpp
  swarm_majority.cpp
  swarm_clearnote.cpp
  swarm_balclass.cpp
  swarm_refundkey.cpp
  swarm_researchkey.cpp
  swarm_onetemplate.cpp
  swarm_mandate0.cpp
  swarm_chalgate.cpp
  swarm_nopenalty.cpp
  swarm_joinflag.cpp
  swarm_seqonce.cpp
  swarm_fiatquote.cpp
  swarm_refundheight.cpp
  swarm_provis.cpp
  swarm_cohortexp.cpp
  swarm_closemargin.cpp
  swarm_redist.cpp
  swarm_assurance.cpp
  swarm_senderedge.cpp
  swarm_poolref.cpp
  swarm_wallclock.cpp
  swarm_watchalert.cpp
  swarm_nbhd.cpp
  swarm_idle.cpp
  swarm_piece32.cpp
  swarm_fwdfee.cpp
  swarm_dnsempty.cpp
  swarm_voucher.cpp
  swarm_manifestcap.cpp
  swarm_rewardmix.cpp
  swarm_workcycle.cpp
  swarm_preimage.cpp
  swarm_peerpath.cpp
  swarm_pareto.cpp
  swarm_explore.cpp
  swarm_community.cpp
  swarm_poolepoch.cpp
  swarm_meshpool.cpp
  swarm_revsplit.cpp
  swarm_governor.cpp
  swarm_vram.cpp
  swarm_cpueval.cpp
  swarm_kvshare.cpp
  swarm_fallback.cpp
  swarm_hashlock.cpp
  swarm_fundsign.cpp
  swarm_piece4m.cpp
  swarm_private.cpp
  swarm_authorsign.cpp
  swarm_refundfirst.cpp
  swarm_slot1.cpp
  swarm_cpucap.cpp
  swarm_evalpair.cpp
  swarm_maxsteps.cpp
  swarm_notrealm.cpp
  swarm_meter0.cpp
  swarm_hforigin.cpp
  swarm_notreplay.cpp
  swarm_publishback.cpp
  swarm_agreement.cpp
  swarm_launchprof.cpp
  swarm_nobroadcast.cpp
  swarm_schedkey.cpp
  swarm_frozenpairs.cpp
  swarm_norealmoney.cpp
  swarm_notmine.cpp
  swarm_notbenchmark.cpp
  swarm_safetyblock.cpp
  swarm_notmultihop.cpp
  swarm_nosidecar.cpp
  swarm_kvnomove.cpp
  swarm_assumedclose.cpp
  swarm_familyonly.cpp
  swarm_notfsdp.cpp
  swarm_notsglang.cpp
  swarm_notwindows.cpp
  swarm_notlmcache.cpp
  swarm_notnixl.cpp
  swarm_notrdma.cpp
  swarm_nocheckpoint.cpp
  swarm_notdiloco.cpp
  swarm_notp95.cpp
  swarm_notttft.cpp
  swarm_notmooncake.cpp
  swarm_notcachehit.cpp
  swarm_notbytes.cpp
  swarm_nottwonode.cpp
  swarm_notholdout.cpp
  swarm_notacquire.cpp
  swarm_nottrajectory.cpp
  swarm_notcert.cpp
  swarm_notflagship.cpp
  swarm_notlivefault.cpp
  swarm_notadopt.cpp
  swarm_notsocket.cpp
  swarm_notmixed.cpp
  swarm_notgrad.cpp
  swarm_notrecover.cpp
  swarm_notrepeat.cpp
  swarm_notcoord.cpp
  swarm_notpay.cpp
  swarm_notcachegain.cpp
  swarm_notnat.cpp
  swarm_notcanary.cpp
  swarm_notpromo.cpp
  swarm_nottrunc.cpp
  swarm_notdup.cpp
  swarm_notlease.cpp
  swarm_notworker.cpp
  swarm_notstale.cpp
  swarm_notquorum.cpp
  swarm_notcollateral.cpp
  swarm_notrefundlive.cpp
  swarm_notrevoke.cpp
  swarm_notpaycrash.cpp
  swarm_notdataset.cpp
  swarm_notexit.cpp
  swarm_notgone.cpp
  swarm_notowner.cpp
  swarm_notsurvivor.cpp
  swarm_notpartial.cpp
  swarm_fault01.cpp
  swarm_fault02.cpp
  swarm_fault10.cpp
  swarm_fault11.cpp
  swarm_notraft.cpp
  swarm_notmtls.cpp
  swarm_notaes.cpp
  swarm_notwitness.cpp
  swarm_notguest.cpp
  swarm_notfalsecompat.cpp
  swarm_fault03.cpp
  swarm_fault05.cpp
  swarm_fault06.cpp
  swarm_fault07.cpp
  swarm_fault09.cpp
  swarm_fault14.cpp
  swarm_fault15.cpp
  swarm_fault16.cpp
  swarm_fault17.cpp
  swarm_fault18.cpp
  swarm_fault19.cpp
  swarm_flag04.cpp
  swarm_fault08.cpp
  swarm_fault12.cpp
  swarm_fault13.cpp
  swarm_flag01.cpp
  swarm_flag02.cpp
  swarm_flag07.cpp
  swarm_flag11.cpp
  swarm_fault04.cpp
  swarm_fault20.cpp
  swarm_flag03.cpp
  swarm_flag05.cpp
  swarm_flag06.cpp
  swarm_flag08.cpp
  swarm_flag09.cpp
  swarm_flag10.cpp
  swarm_flag12.cpp
  swarm_flag13.cpp
  swarm_notp50.cpp
  swarm_notgpuhours.cpp
  swarm_notroute.cpp
  swarm_notthroughput.cpp
  swarm_notbaseline.cpp
  swarm_notfailover.cpp
  swarm_notckpttime.cpp
  swarm_notuseful.cpp
  swarm_notplacement.cpp
  swarm_notfinetune.cpp
  swarm_notmultinode.cpp
  swarm_nottasklat.cpp
  swarm_notvram.cpp
  swarm_notfees.cpp
  swarm_notcoordoff.cpp
  swarm_notcontrib.cpp
  swarm_notrollout.cpp
  swarm_notworkflow.cpp
  swarm_notcacheoff.cpp
  swarm_notquality.cpp
  swarm_nottraintoks.cpp
  swarm_notnetbytes.cpp
  swarm_notwall.cpp
  swarm_notablation.cpp
  swarm_notframework.cpp
  swarm_notplaceoff.cpp
  swarm_notisland.cpp
  swarm_notaccel.cpp
  swarm_notresource.cpp
  swarm_notomit.cpp
  swarm_notfallback.cpp
  swarm_notenv.cpp
  swarm_notcrash.cpp
  swarm_notcatalog.cpp
  swarm_notdep.cpp
  swarm_notsubscribe.cpp
  swarm_notinit.cpp
  swarm_notidentity.cpp
  swarm_notrpc.cpp
  swarm_notsidecar2.cpp
  swarm_notdurable.cpp
  swarm_notpackage.cpp
  swarm_notdiscover.cpp
  swarm_notelectric.cpp
  swarm_notlearner.cpp
  swarm_notmedium.cpp
  swarm_notproposer.cpp
  swarm_notsandbox.cpp
  swarm_nota2a.cpp
  swarm_notfamilydir.cpp
  swarm_notlongbase.cpp
  swarm_notmcp.cpp
  swarm_notreport.cpp
  swarm_notscripthop.cpp
  swarm_notdispute.cpp
  swarm_notlaterpage.cpp
  swarm_notcallerqual.cpp
  swarm_notgradleak.cpp
  swarm_notchurn.cpp
  swarm_notwear.cpp
  swarm_notnetcost.cpp
  swarm_notprompt.cpp
  swarm_notspendkey.cpp
  swarm_notmainnet.cpp
  swarm_notrealmoney.cpp
  swarm_notrotate.cpp
  swarm_notselfeval.cpp
  swarm_notsecp.cpp
  swarm_notagreement.cpp
  swarm_notadmin.cpp
  swarm_notfork.cpp
  swarm_notprofit.cpp
  swarm_notclearing.cpp
  swarm_notcredential.cpp
  swarm_notfitness.cpp
  swarm_notnccl.cpp
  swarm_notassumption.cpp
  swarm_notcapped.cpp
  swarm_notprefetch.cpp
  swarm_notvote.cpp
  swarm_notprofile.cpp
  swarm_notspacing.cpp
  swarm_notfiat.cpp
  swarm_notpiece.cpp
  swarm_notbody.cpp
  swarm_notniche.cpp
  swarm_nottax.cpp
  swarm_notneighborhood.cpp
  swarm_notunfunded.cpp
  swarm_notkvpage.cpp
  swarm_notsameatoms.cpp
  swarm_notmesh.cpp
  swarm_notresearchkey.cpp
  swarm_notfourbal.cpp
  swarm_notcohort.cpp
  swarm_notfrozen.cpp
  swarm_notcurrency.cpp
  swarm_notfallbackpeer.cpp
  swarm_notforwarder.cpp
  swarm_notrevenue.cpp
  swarm_notexplore.cpp
  swarm_notfirstedge.cpp
  swarm_notmargin.cpp
  swarm_notredistrib.cpp
  swarm_nottrainlicense.cpp
  swarm_notsequence.cpp
  swarm_notmanifestcap.cpp
  swarm_notl1.cpp
  swarm_notschedwit.cpp
  swarm_notgovernor.cpp
  swarm_notseeder.cpp
  swarm_notjoinon.cpp
  swarm_notmodelon.cpp
  swarm_notrefundfirst.cpp
  swarm_notvramzero.cpp
  swarm_nothflineage.cpp
  swarm_notdistinct.cpp
  swarm_notautospend.cpp
  swarm_notdns.cpp
  swarm_notopeninfer.cpp
  swarm_notrefundleaf.cpp
  swarm_notsponsor.cpp
  swarm_notppmfee.cpp
  swarm_notqualrealm.cpp
  swarm_notzerostep.cpp
  swarm_nothashleaf.cpp
  swarm_notweek.cpp
  swarm_notsigsize.cpp
  swarm_notquorumdom.cpp
  swarm_notcpueval.cpp
  swarm_notgpuoff.cpp
  swarm_notmhq.cpp
  swarm_not14400.cpp
  swarm_notminthread.cpp
  swarm_nottxid.cpp
  swarm_notnoseal.cpp
  swarm_notstall.cpp
  swarm_notnosess.cpp
  swarm_notnockpt.cpp
  swarm_notoneproc.cpp
  swarm_notcudaops.cpp
  swarm_notnordma.cpp
  swarm_notmoonabs.cpp
  swarm_notnofsdp.cpp
  swarm_notnosglang.cpp
  swarm_notnodiloco.cpp
  swarm_notnixlabs.cpp
  swarm_notnomult.cpp
  swarm_notnopct.cpp
  swarm_notnobytes.cpp
  swarm_notnohit.cpp
  swarm_notnowin.cpp
  swarm_notnottft.cpp
  swarm_notnomixed.cpp
  swarm_notnogain.cpp
  swarm_notnofund.cpp
  swarm_notslots.cpp
  swarm_notprivateoff.cpp
  swarm_notclosetmpl.cpp
  swarm_notmandate.cpp
  free_grant.cpp
  router.cpp
  transport_pq.cpp
  bridge.cpp
  transfer.cpp
  funding.cpp
  release.cpp
  economy.cpp
  feed.cpp
  qualification.cpp
  identity.cpp
  acl.cpp
  safety.cpp
  access_policy.cpp
  community.cpp
  reciprocity.cpp
  http_bridge.cpp
  cores.cpp
  catalog.cpp
  generate.cpp
  helper.cpp
  pq1_runtime.cpp
  piece_picker.cpp
  provider_exchange.cpp
  model_nat.cpp
  auto_storage.cpp
  piece_ranges.cpp
  supervisor.cpp
  reachability.cpp
  relay_reserve.cpp
  provider_route.cpp
  search.cpp
  canonical_codec.cpp
  pq_delegation.cpp
  pq_transport_binding.cpp
  git_snapshot.cpp
  workspace_journal.cpp
  research_graph.cpp
  training_evidence.cpp
  model_admission.cpp
  action_authority.cpp
  realm_channel.cpp
  admission_routes.cpp
  workspace_git.cpp
  research_index.cpp
  training_profile.cpp
  session_gate.cpp
  capability_bridge.cpp
  research_rpc.cpp
  lab_api.cpp
  lab_disk.cpp
  lab_ancestry.cpp
  lab_guard_client.cpp
  lab_exchange.cpp
  lab_peer.cpp
  training_integer.cpp
  training_launch.cpp
  research_views.cpp
  guard_machine.cpp
  lab_journey.cpp
  lab_wire.cpp
  lab_assurance.cpp
  lab_gateway.cpp
  lab_release.cpp
  lab_research_gate.cpp
  lab_store.cpp
  lab_remaining.cpp
  migrate_035.cpp
  model_panel.cpp
  lab035_cli.cpp
  bounty.cpp
  compute_economy.cpp
  bounty_eval.cpp
  file_stream.cpp
  direct_seed.cpp
  profile.cpp
  event_journal.cpp
  model_watch.cpp
  subscription_mandate.cpp
  cloud_layout.cpp
  s3_client.cpp
  s3_store.cpp
  verified_manifest.cpp
  transfer_session.cpp
  import_plan.cpp
  package_bundle.cpp
  erasure_store.cpp
  erasure_manifest.cpp
  package_export.cpp
  package_pjson.cpp
  package_core.cpp
  package_documents.cpp
  package_acquisition.cpp
  package_install.cpp
  package_runtime.cpp
  package_channel.cpp
  package_economy.cpp
  package_execute.cpp
  upload_scheduler.cpp
  upload_scheduler_drr.cpp
  bootstrap_distributor.cpp
  origin_broker.cpp
  metadata_gossip.cpp
  query_router.cpp
  index_reconcile.cpp
  source_local.cpp
  import_coordinator.cpp
  source_huggingface.cpp
  source_xet.cpp
  source_torrent.cpp
  provenance.cpp
  oci_modelpack.cpp
  registry_resolver.cpp
  source_registry.cpp
  object_layout.cpp
  subpiece.cpp
  bulk_controller.cpp
  io_executor.cpp
  hello_caps.cpp
  helper_network02.cpp
  source_policy.cpp
  residency_state.cpp
  multipart_journal.cpp
  operation_budget.cpp
  physical_dedup.cpp
  selective_files.cpp
  lan_discovery.cpp
  capability_types.cpp
  capability_core.cpp
  capability_residency.cpp
  capability_bytes.cpp
  capability_exec.cpp
  capability_control.cpp
  capability_fuse.cpp
  capability_runtime.cpp
  capability_compose.cpp
  capability_prefetch.cpp
  capability_ensure.cpp
  capability_peer.cpp
  capability_moe.cpp
  capability_sdk.cpp
  hcp_codec.cpp
    hcp_cr11.cpp
    hcp_crl12.cpp
    hcp_engine.cpp
)
target_link_libraries(bitcoin_modelnet
  PUBLIC
    core_interface
    bitcoin_crypto
    bitcoin_util
    bitcoin_common
    univalue
    bitcoinpqc
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
  PRIVATE
    bitcoin_consensus
)
target_include_directories(bitcoin_modelnet
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)
if(WIN32)
  # POSIX shims for the model plane. Linux and macOS keep the system headers.
  target_include_directories(bitcoin_modelnet BEFORE PUBLIC
    ${PROJECT_SOURCE_DIR}/src/compat/mingw)
  target_compile_options(bitcoin_modelnet PRIVATE
    "SHELL:-include ${PROJECT_SOURCE_DIR}/src/compat/mingw/posix_extra.h")
  target_link_libraries(bitcoin_modelnet PUBLIC ws2_32)
endif()

set(_btx_crl12_schema "${PROJECT_SOURCE_DIR}/contrib/modelnet/crl12/schemas/CognitiveReserveLayer.schema.json")
set(_btx_crl12_ops "${PROJECT_SOURCE_DIR}/contrib/modelnet/crl12/schemas/operations-v1.2.json")
set(_btx_cr11_schema "${PROJECT_SOURCE_DIR}/contrib/modelnet/crl12/compatibility/v1_1/schemas/CognitiveReserve.schema.json")
set(_btx_cr11_ops "${PROJECT_SOURCE_DIR}/contrib/modelnet/crl12/compatibility/v1_1/schemas/operations-v1.1.json")
if(EXISTS "${_btx_crl12_schema}")
  file(SHA384 "${_btx_crl12_schema}" BTX_CRL12_SCHEMA_SHA384)
  target_compile_definitions(bitcoin_modelnet PRIVATE BTX_CRL12_SCHEMA_SHA384="${BTX_CRL12_SCHEMA_SHA384}")
endif()
if(EXISTS "${_btx_crl12_ops}")
  file(SHA384 "${_btx_crl12_ops}" BTX_CRL12_OPERATIONS_SHA384)
  target_compile_definitions(bitcoin_modelnet PRIVATE BTX_CRL12_OPERATIONS_SHA384="${BTX_CRL12_OPERATIONS_SHA384}")
endif()
if(EXISTS "${_btx_cr11_schema}")
  file(SHA384 "${_btx_cr11_schema}" BTX_CR11_SCHEMA_SHA384)
  target_compile_definitions(bitcoin_modelnet PRIVATE BTX_CR11_SCHEMA_SHA384="${BTX_CR11_SCHEMA_SHA384}")
endif()
if(EXISTS "${_btx_cr11_ops}")
  file(SHA384 "${_btx_cr11_ops}" BTX_CR11_OPERATIONS_SHA384)
  target_compile_definitions(bitcoin_modelnet PRIVATE BTX_CR11_OPERATIONS_SHA384="${BTX_CR11_OPERATIONS_SHA384}")
endif()

add_executable(btx-modeld ../modeld.cpp)
set_target_properties(btx-modeld PROPERTIES OUTPUT_NAME btx-modeld)
target_link_libraries(btx-modeld core_interface)
target_link_btx_linux_archive_group(btx-modeld bitcoin_modelnet ${BTX_BASE_ARCHIVES})

add_executable(btx-modelcheck ../modelcheck.cpp)
set_target_properties(btx-modelcheck PROPERTIES OUTPUT_NAME btx-modelcheck)
target_link_libraries(btx-modelcheck core_interface)
target_link_btx_linux_archive_group(btx-modelcheck bitcoin_modelnet ${BTX_BASE_ARCHIVES})

add_executable(btx-open ../btx-open.cpp)
set_target_properties(btx-open PROPERTIES OUTPUT_NAME btx-open)
target_link_libraries(btx-open core_interface)
target_link_btx_linux_archive_group(btx-open bitcoin_modelnet ${BTX_BASE_ARCHIVES})

add_executable(btx-capabilityd ../capabilityd.cpp)
set_target_properties(btx-capabilityd PROPERTIES OUTPUT_NAME btx-capabilityd)
target_link_libraries(btx-capabilityd core_interface)
target_link_btx_linux_archive_group(btx-capabilityd bitcoin_modelnet ${BTX_BASE_ARCHIVES})

add_executable(btx-capability ../btx-capability.cpp)
set_target_properties(btx-capability PROPERTIES OUTPUT_NAME btx-capability)
target_link_libraries(btx-capability core_interface)
target_link_btx_linux_archive_group(btx-capability bitcoin_modelnet ${BTX_BASE_ARCHIVES})

add_executable(btx-hcpd ../hcpd.cpp)
set_target_properties(btx-hcpd PROPERTIES OUTPUT_NAME btx-hcpd)
target_link_libraries(btx-hcpd core_interface)
target_link_btx_linux_archive_group(btx-hcpd bitcoin_modelnet ${BTX_BASE_ARCHIVES})

add_executable(btx-hosted ../hosted.cpp)
set_target_properties(btx-hosted PROPERTIES OUTPUT_NAME btx-hosted)
target_link_libraries(btx-hosted core_interface)
target_link_btx_linux_archive_group(btx-hosted bitcoin_modelnet ${BTX_BASE_ARCHIVES})

install(TARGETS btx-modeld btx-modelcheck btx-open btx-capabilityd btx-capability btx-hcpd btx-hosted RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
