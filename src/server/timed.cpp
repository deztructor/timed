/***************************************************************************
**                                                                        **
**   Copyright (C) 2009-2010 Nokia Corporation.                           **
**                                                                        **
**   Author: Ilya Dogolazky <ilya.dogolazky@nokia.com>                    **
**   Author: Simo Piiroinen <simo.piiroinen@nokia.com>                    **
**   Author: Victor Portnov <ext-victor.portnov@nokia.com>                **
**                                                                        **
**     This file is part of Timed                                         **
**                                                                        **
**     Timed is free software; you can redistribute it and/or modify      **
**     it under the terms of the GNU Lesser General Public License        **
**     version 2.1 as published by the Free Software Foundation.          **
**                                                                        **
**     Timed is distributed in the hope that it will be useful, but       **
**     WITHOUT ANY WARRANTY;  without even the implied warranty  of       **
**     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.               **
**     See the GNU Lesser General Public License  for more details.       **
**                                                                        **
**   You should have received a copy of the GNU  Lesser General Public    **
**   License along with Timed. If not, see http://www.gnu.org/licenses/   **
**                                                                        **
***************************************************************************/
#define _BSD_SOURCE

#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <QDBusConnection>
#include <QDBusConnectionInterface>

#include <ContextProvider>

#include <timed/interface>
#include <qm/log>

#include "adaptor.h"
#include "backup.h"
#include "timed.h"
#include "settings.h"
#include "tz.h"

static void spam()
{
#if NO_SPAM
  time_t now = time(NULL) ;
  for(int i=0; i<12; ++i)
  {
    time_t then = now + i*30*24*60*60 ;
    struct tm t ;
    localtime_r(&then, &t) ;
    log_debug("i=%d, time:%ld, t.tm_gmtoff=%ld", i, then, t.tm_gmtoff) ;
  }
#endif
#if NO_SPAM
  qlonglong A=1111, B=2222 ;
  QString str = "bebe " ;
  str += QString(" timestamp: %1/%2").arg(A).arg(B) ;
  qDebug() << str ;
#endif
#if NO_SPAM
  log_info("AA") ;
  for(nanotime_t x(-2,0); x.sec()<3; x+=nanotime_t(0,100000000))
  {
    QString res ;
    QTextStream os(&res) ;
    os << x ;
    log_info("%s=%ld", string_q_to_std(res).c_str(), x.to_time_t()) ;
  }
  log_info("BB") ;
#endif
}

Timed::Timed(int ac, char **av) : QCoreApplication(ac, av)
{
  spam() ;
  halted = "" ; // XXX: remove it, as we don't want to halt anymore
  log_debug() ;

  init_unix_signal_handler() ;
  log_debug() ;

  init_scratchbox_mode() ;
  log_debug() ;

  init_act_dead() ;
  log_debug() ;

  init_configuration() ;
  log_debug() ;

  init_customization() ;
  log_debug() ;

  init_read_settings() ;
  log_debug() ;

  init_create_event_machine() ;
  log_debug() ;

  init_context_objects() ;
  log_debug() ;

  init_backup_object() ;
  log_debug() ;

  init_main_interface_object() ;
  log_debug() ;

  init_backup_dbus_name() ;
  log_debug() ;

  init_main_interface_dbus_name() ;
  log_debug() ;

  init_load_events() ;
  log_debug() ;

  init_cellular_services() ;
  log_debug() ;

#if 0
  save_time_to_file_timer = new QTimer ;
  QObject::connect(save_time_to_file_timer, SIGNAL(timeout()), this, SLOT(save_time_to_file())) ;
  save_time_to_file() ;
#endif

  log_debug("starting event mahine") ;

  init_start_event_machine() ;
  log_debug() ;

  log_debug("applying time zone settings") ;
  init_apply_tz_settings() ;

  log_info("daemon is up and running") ;
}

// * Start Unix signal handling
void Timed::init_unix_signal_handler()
{
  signal_object = UnixSignal::object() ;
  QObject::connect(signal_object, SIGNAL(signal(int)), this, SLOT(unix_signal(int))) ;
  signal_object->handle(SIGINT) ;
  signal_object->handle(SIGTERM) ;
  signal_object->handle(SIGCHLD) ;
}

// * Condition "running inside of scratchbox" is detected
void Timed::init_scratchbox_mode()
{
#if F_SCRATCHBOX
  const char *path = getenv("PATH") ;
  scratchbox_mode = path && strstr(path, "scratchbox") ; // XXX: more robust sb detection?
  log_info("%s" "SCRATCHBOX detected", scratchbox_mode ? "" : "no ") ;
#else
  scratcbox_mode = false ;
#endif
}

// * Condition "running in ACT DEAD mode" is detected.
// * When running on Harmattan device (not scratchbox!):
//      some sanity checks are performed.
void Timed::init_act_dead()
{
#if F_ACTING_DEAD
  act_dead_mode = access("/tmp/ACT_DEAD", F_OK) == 0 ;
  if (not scratchbox_mode)
  {
    bool user_mode = access("/tmp/USER", F_OK) == 0 ;
    log_assert(act_dead_mode != user_mode) ;
  }
  log_info("running in %s mode", act_dead_mode ? "ACT_DEAD" : "USER") ;
#else
  act_dead_mode = false ;
#endif
}

// * Reading configuration file
// * Warning if no exists (which is okey)
void Timed::init_configuration()
{
  iodata::storage *config_storage = new iodata::storage ;
  config_storage->set_primary_path(configuration_path()) ;
  config_storage->set_validator(configuration_type(), "config_t") ;

  iodata::record *c = config_storage->load() ;
  log_assert(c, "loading configuration settings failed") ;

  if(config_storage->source()==0)
    log_info("configuration loaded from '%s'", configuration_path()) ;
  else
    log_warning("configuration file '%s' corrupted or non-existing, using default values", configuration_path()) ;

  events_path = c->get("queue_path")->str() ; // TODO: make C++ variables match data fields
  settings_path = c->get("settings_path")->str() ;
  threshold_period_long = c->get("queue_threshold_long")->value() ;
  threshold_period_short = c->get("queue_threshold_short")->value() ;
  ping_period = c->get("voland_ping_sleep")->value() ;
  ping_max_num = c->get("voland_ping_retries")->value() ;
  delete c ;
#if 0 // XXX: remove it for ever
  save_time_path = c->get("saved_utc_time_path")->str() ;
#endif
}

static bool parse_boolean(const string &str)
{
  return str == "true" || str == "True" || str == "1" ;
}

// * read customization data provided by customization package
void Timed::init_customization()
{
  iodata::storage *storage = new iodata::storage ;
  storage->set_primary_path(customization_path()) ;
  storage->set_validator(customization_type(), "customization_t") ;

  iodata::record *c = storage->load() ;
  log_assert(c, "loading customization settings failed") ;

  if(storage->source()==0)
    log_info("customization loaded from '%s'", customization_path()) ;
  else
    log_warning("customization file '%s' corrupted or non-existing, using default values", customization_path()) ;

  format24_by_default = parse_boolean(c->get("format24")->str()) ;
  nitz_supported = parse_boolean(c->get("useNitz")->str()) ;
  auto_time_by_default = parse_boolean(c->get("autoTime")->str()) ;
  guess_tz_by_default = parse_boolean(c->get("guessTz")->str()) ;
  tz_by_default = c->get("tz")->str() ;

  if (not nitz_supported and auto_time_by_default)
  {
    log_warning("automatic time update disabled because nitz is not supported in the device") ;
    auto_time_by_default = false ;
  }

  delete c ;
}

// * read settings
// * apply customization defaults, if needed
void Timed::init_read_settings()
{
#if 0
  cust_settings = new customization_settings();
  cust_settings->load();
#endif

  settings_storage = new iodata::storage ;
  settings_storage->set_primary_path(settings_path) ;
  settings_storage->set_secondary_path(settings_path+".bak") ;
  settings_storage->set_validator(settings_file_type(), "settings_t") ;

  iodata::record *tree = settings_storage->load() ;

  log_assert(tree, "loading settings failed") ;

#define apply_cust(key, val)  do { if (tree->get(key)->value() < 0) tree->add(key, val) ; } while(false)
  apply_cust("format_24", format24_by_default) ;
  apply_cust("time_nitz", auto_time_by_default) ;
  apply_cust("local_cellular", guess_tz_by_default) ;
#undef apply_cust

#if 0
  // we can't do it here:
  //   first get dbus name (as a mutex), then fix the files
#endif

  settings = new source_settings(this) ; // TODO: use tz_by_default here
  settings->load(tree) ;

  delete tree ;
}

void Timed::init_create_event_machine()
{
  am = new machine(this) ;
  log_debug("am=new machine done") ;
  q_pause = NULL ;

  am->device_mode_detected(not act_dead_mode) ; // TODO: avoid "not" here

  short_save_threshold_timer = new simple_timer(threshold_period_short) ;
  long_save_threshold_timer = new simple_timer(threshold_period_long) ;
  QObject::connect(short_save_threshold_timer, SIGNAL(timeout()), this, SLOT(queue_threshold_timeout())) ;
  QObject::connect(long_save_threshold_timer, SIGNAL(timeout()), this, SLOT(queue_threshold_timeout())) ;

  QObject::connect(am, SIGNAL(child_created(unsigned,int)), this, SLOT(register_child(unsigned,int))) ;
  clear_invokation_flag() ;

  ping = new pinguin(ping_period, ping_max_num) ;
  QObject::connect(am, SIGNAL(voland_needed()), ping, SLOT(voland_needed())) ;
  QObject::connect(this, SIGNAL(voland_registered()), ping, SLOT(voland_registered())) ;

  QObject::connect(am, SIGNAL(queue_to_be_saved()), this, SLOT(event_queue_changed())) ;

  QDBusConnectionInterface *bus_ifc = Maemo::Timed::Voland::bus().interface() ;
  bool voland_present = bus_ifc->isServiceRegistered(Maemo::Timed::Voland::service()) ;

  if(voland_present)
  {
    log_info("Voland service %s detected", Maemo::Timed::Voland::service()) ;
    emit voland_registered() ;
  }

  voland_watcher = new QDBusServiceWatcher((QString)Maemo::Timed::Voland::service(), Maemo::Timed::Voland::bus()) ;
  QObject::connect(voland_watcher, SIGNAL(serviceOwnerChanged(QString,QString,QString)), this, SLOT(system_owner_changed(QString,QString,QString))) ;
  QObject::connect(this, SIGNAL(voland_registered()), am, SIGNAL(voland_registered())) ;
  QObject::connect(this, SIGNAL(voland_unregistered()), am, SIGNAL(voland_unregistered())) ;
}

void Timed::init_context_objects()
{
  (new ContextProvider::Service(Maemo::Timed::bus()))->setAsDefault() ;
  log_debug("(new ContextProvider::Service(Maemo::Timed::bus()))->setAsDefault()") ;

  ContextProvider::Property("Alarm.Trigger") ;
  ContextProvider::Property("Alarm.Present") ;
  ContextProvider::Property("/com/nokia/time/time_zone/oracle") ;
  time_operational_p = new ContextProvider::Property("/com/nokia/time/system_time/operational") ;
  time_operational_p->setValue(am->is_epoch_open()) ;
  QObject::connect(am, SIGNAL(next_bootup_event(int)), this, SLOT(send_next_bootup_event(int))) ;
}


void Timed::init_backup_object()
{
  QObject *backup_object = new QObject ;
  new com_nokia_timed_backup(this, backup_object) ;
  // XXX: what if we're using system bus: how should backup know this?
  // TODO: if using system bus, keep track of started/terminated sessions? (omg!)
  QDBusConnection conn = Maemo::Timed::bus() ;
  const char * const path = "/com/nokia/timed/backup" ;
  if (conn.registerObject(path, backup_object))
    log_info("backup interface object registered on path '%s'", path) ;
  else
  {
    log_critical("failed to register backup object on path '%s': %s", path, conn.lastError().message().toStdString().c_str()) ;
    log_critical("backup/restore not available") ;
  }
}

void Timed::init_main_interface_object()
{
  new com_nokia_time(this) ;
  QDBusConnection conn = Maemo::Timed::bus() ;
  const char * const path = Maemo::Timed::objpath() ;
  if (conn.registerObject(path, this))
    log_info("main interface object registered on path '%s'", path) ;
  else
    log_critical("remote methods not available; failed to register dbus object: %s", Maemo::Timed::bus().lastError().message().toStdString().c_str()) ;
  // XXX:
  // probably it's a good idea to terminate if failed
  // (usually it means, the dbus connection is not available)
  // but on the other hand we can still provide some services (like setting time and zone)
  // Anyway, we will terminate if the mutex like name is not available
}

void Timed::init_backup_dbus_name()
{
  // We're using an another name for backup interface
  //   to avoid mess while switching to system bus and back again (later)
  // XXX: But for now it's just the same connection as com.nokia.time
  QDBusConnection conn = Maemo::Timed::bus() ;
  const char * const name = "com.nokia.timed.backup" ;
  const string conn_name = conn.name().toStdString() ;
  if (conn.registerService(name))
    log_info("service name '%s' registered on bus '%s'", name, conn_name.c_str()) ;
  else
  {
    const string msg = conn.lastError().message().toStdString() ;
    log_critical("can't register service '%s' on bus '%s': '%s'", name, conn_name.c_str(), msg.c_str()) ;
    log_critical("backup/restore not available") ;
  }
}

void Timed::init_main_interface_dbus_name()
{
  // We're misusing the dbus name as a some kind of mutex:
  //   only one instance of timed is allowed to run.
  // This is the why we can't drop the name later.

  const string conn_name = Maemo::Timed::bus().name().toStdString() ;
  if (Maemo::Timed::bus().registerService(Maemo::Timed::service()))
    log_info("service name '%s' registered on bus '%s'", Maemo::Timed::service(), conn_name.c_str()) ;
  else
  {
    const string msg = Maemo::Timed::bus().lastError().message().toStdString() ;
    log_critical("can't register service '%s' on bus '%s': '%s'", Maemo::Timed::service(), conn_name.c_str(), msg.c_str()) ;
    log_critical("aborting") ;
    ::exit(1) ;
  }
}

void Timed::init_load_events()
{
  event_storage = new iodata::storage ;
  event_storage->set_primary_path(events_path) ;
  event_storage->set_secondary_path(events_path+".bak") ;
  event_storage->set_validator(event_queue_type(), "event_queue_t") ;

  iodata::record *events = event_storage->load() ;

  log_assert(events) ;

  am->load(events) ;

  delete events ;
}

void Timed::init_start_event_machine()
{
  if(not settings_storage->fix_files(false))
    log_critical("can't fix the primary settings file") ;
  if(not event_storage->fix_files(false))
    log_critical("can't fix the primary event queue file") ;
  am->process_transition_queue() ;
  am->start() ;
}

void Timed::init_cellular_services()
{
  cellular_handler *nitz_object = cellular_handler::object() ;
  int nitzrez = QObject::connect(nitz_object, SIGNAL(cellular_data_received(const cellular_info_t &)), this, SLOT(nitz_notification(const cellular_info_t &))) ;
  log_debug("nitzrez=%d", nitzrez) ;

  tz_oracle = new tz_oracle_t ;
  QObject::connect(tz_oracle, SIGNAL(tz_detected(olson *, tz_suggestions_t)), this, SLOT(tz_by_oracle(olson *, tz_suggestions_t))) ;
  QObject::connect(nitz_object, SIGNAL(cellular_data_received(const cellular_info_t &)), tz_oracle, SLOT(nitz_data(const cellular_info_t &))) ;
}

void Timed::init_apply_tz_settings()
{
  settings->postload_fix_manual_zone() ;
  settings->postload_fix_manual_offset() ;
  if(settings->check_target(settings->etc_localtime()) != 0)
    invoke_signal() ;
}


// Move the stuff below to machine:: class

cookie_t Timed::add_event(cookie_t remove, const Maemo::Timed::event_io_t &x, const QDBusMessage &message)
{
  if(remove.is_valid() && am->find_event(remove)==NULL)
  {
    log_error("[%d]: cookie not found, event can't be replaced", remove.value()) ;
    return cookie_t() ;
  }

  cookie_t c = am->add_event(&x, true, NULL, &message) ; // message is given, but no creds
  log_debug() ;
  QMap<QString,QString>::const_iterator test = x.attr.txt.find("TEST") ;
  log_debug() ;
  if(test!=x.attr.txt.end())
    log_debug("TEST event: '%s', cookie=%d", test.value().toStdString().c_str(), c.value()) ;
  log_debug() ;
  if(c.is_valid() && remove.is_valid() && !am->cancel_by_cookie(remove))
    log_critical("[%d]: failed to remove event", remove.value()) ;
  return c ;
}

void Timed::add_events(const Maemo::Timed::event_list_io_t &events, QList<QVariant> &res, const QDBusMessage &message)
{
  if(events.ee.size()==0)
  {
    log_info("empty event list to add, ignoring") ;
    return ;
  }
  QMap<QString,QString>::const_iterator test = events.ee[0].attr.txt.find("TEST") ;
  if(test!=events.ee[0].attr.txt.end())
    log_debug("TEST list of %d events: '%s'", events.ee.size(), test.value().toStdString().c_str()) ;
  am->add_events(events, res, message) ;
}

bool Timed::dialog_response(cookie_t c, int value)
{
  log_debug("Responded: %d(value=%d)", c.value(), value) ;
  return am->dialog_response(c, value) ;
}

void Timed::system_owner_changed(const QString &name, const QString &oldowner, const QString &newowner)
{
  bool name_match = name==Maemo::Timed::Voland::service() ;
  if(name_match && oldowner.isEmpty() && !newowner.isEmpty())
    emit voland_registered() ;
  else if(name_match && !oldowner.isEmpty() && newowner.isEmpty())
    emit voland_unregistered() ;
#define __qstr(a) (a.isEmpty()?"<empty>":a.toStdString().c_str())
  if(name_match)
    log_info("Service %s owner changed from %s to %s", __qstr(name), __qstr(oldowner), __qstr(newowner)) ;
  else
    log_error("expecing notification about '%s' got about '%s'", Maemo::Timed::Voland::service(), name.toStdString().c_str()) ;
#undef __qstr
}

void Timed::send_next_bootup_event(int value)
{
  QDBusConnection dsme = QDBusConnection::systemBus() ;
  QString path = Maemo::Timed::objpath() ;
  QString iface = Maemo::Timed::interface() ;
  QString signal = "next_bootup_event" ;
  QDBusMessage m = QDBusMessage::createSignal(path, iface, signal) ;
  m << value ;
  if(dsme.send(m))
    log_info("signal %s(%d) sent", string_q_to_std(signal).c_str(), value) ;
  else
    log_error("Failed to send the signal %s(%d) on system bus: %s", string_q_to_std(signal).c_str(), value, dsme.lastError().message().toStdString().c_str()) ;
}

void Timed::event_queue_changed()
{
  bool running = short_save_threshold_timer->isActive() ;
  if(running)
    short_save_threshold_timer->stop() ;
  else
    long_save_threshold_timer->start() ;
  short_save_threshold_timer->start() ;
}

void Timed::queue_threshold_timeout()
{
  short_save_threshold_timer->stop() ;
  long_save_threshold_timer->stop() ;
  int method_index = this->metaObject()->indexOfMethod("save_event_queue()") ;
  QMetaMethod method = this->metaObject()->method(method_index) ;
  method.invoke(this, Qt::QueuedConnection) ;
}

/*
 * xxx
 * These are the "stupid and simple" backup methods.
 * Just like the doctor ordered. :)
 * The chmod is a workaround for backup-framework crash bug.
 */

void Timed::save_event_queue()
{
  iodata::record *queue = am->save() ;
  int res = event_storage->save(queue) ;

  if(res==0) // primary file
    log_info("event queue written") ;
  else if(res==1)
    log_warning("event queue written to secondary file") ;
  else
    log_critical("event queue can't be saved") ;

  delete queue ;
}

void Timed::save_settings()
{
  iodata::record *tree = settings->save() ;
  int res = settings_storage->save(tree) ;

  if(res==0) // primary file
    log_info("wall clock settings written") ;
  else if(res==1)
    log_warning("wall clock settings written to secondary file") ;
  else
    log_critical("wall clock settings can't be saved") ;

  delete tree ;
}

void Timed::invoke_signal(const nanotime_t &back)
{
  systime_back += back ;
  if(signal_invoked)
    return ;
  signal_invoked = true ;
  int methodIndex = this->metaObject()->indexOfMethod("send_time_settings()") ;
  QMetaMethod method = this->metaObject()->method(methodIndex);
  method.invoke(this, Qt::QueuedConnection);
  log_assert(q_pause==NULL) ;
  q_pause = new queue_pause(am) ;
  log_debug("new q_pause=%p", q_pause) ;
}

void Timed::send_time_settings()
{
  log_debug() ;
  log_info("settings->cellular_zone='%s'", settings->cellular_zone->zone().c_str()) ;
  nanotime_t diff = systime_back ;
  clear_invokation_flag() ;
  save_settings() ;
  settings->fix_etc_localtime() ;
  emit settings_changed(settings->get_wall_clock_info(diff), !diff.is_zero()) ;
  // emit settings_changed_1(systime) ;
  am->reshuffle_queue(diff) ;
  if(q_pause)
  {
    delete q_pause ;
    q_pause = NULL ;
  }
}

#if 0
void Timed::save_time_to_file()
{
  save_time_to_file_timer->stop() ;

  if(FILE *fp = fopen(save_time_path.c_str(), "w"))
  {
    const int time_length = 32 ;
    char value[time_length+1] ;

    time_t tick = time(NULL) ;
    struct tm tm ;
    gmtime_r(&tick, &tm) ;
    strftime(value, time_length, "%F %T", &tm) ;

    fprintf(fp, "%s\n", value) ;
    if(fclose(fp)==0)
      log_info("current time (%s) saved in %s", value, save_time_path.c_str()) ;
    else
      log_error("can't write to file %s: %m", save_time_path.c_str()) ;
  }
  else
    log_error("can't open file %s: %m", save_time_path.c_str()) ;

  save_time_to_file_timer->start(1000*3600) ; // 1 hour
}
#endif

void Timed::unix_signal(int signo)
{
  switch(signo)
  {
    default:
      log_info("unix signal %d [%s] detected", signo, strsignal(signo)) ;
      break ;
    case SIGCHLD:
      int status ;
      while(pid_t pid = waitpid(-1, &status, WNOHANG))
      {
        if(pid==-1 && errno==EINTR)
        {
          log_info("waitpid() interrupted, retrying...") ;
          continue ;
        }
        else if(pid==-1)
        {
          if(errno!=ECHILD)
            log_error("waitpid() failed: %m") ;
          break ;
        }
        unsigned cookie = children.count(pid) ? children[pid] : 0 ;
        string name = str_printf("the child pid=%d", pid) ;
        if(cookie)
          name += str_printf(" [cookie=%d]", cookie) ;
        else
          name += " (unknown cookie)" ;
        if(WIFEXITED(status))
          log_info("%s exited with status %d", name.c_str(), WEXITSTATUS(status)) ;
        else if(WIFSIGNALED(status))
          log_info("%s killed by signal %d", name.c_str(), WTERMSIG(status)) ;
        else
        {
          log_info("%s changed status", name.c_str()) ;
          continue ;
        }
        children.erase(pid) ;
      }
      break ;
    case SIGINT:
      log_info("Keyboard interrupt, oh weh... bye") ;
      quit() ;
      break ;
    case SIGTERM:
      log_info("Termination signal... bye") ;
      quit() ;
      break ;
  }
}

void Timed::nitz_notification(const cellular_info_t &ci)
{
  log_debug() ;
  log_info("nitz_notification: %s", ci.to_string().c_str()) ;
  settings->cellular_information(ci) ;
  log_debug() ;
}

void Timed::tz_by_oracle(olson *tz, tz_suggestions_t s)
{
  log_debug("time zone '%s' magicaly detected", tz->name().c_str()) ;
  settings->cellular_zone->value = tz->name() ;
  settings->cellular_zone->suggestions = s ;
  if(settings->local_cellular)
  {
    settings->fix_etc_localtime() ;
    update_oracle_context(true) ;
  }
  invoke_signal() ;
}

void Timed::update_oracle_context(bool set)
{
  static ContextProvider::Property oracle_p("/com/nokia/time/timezone/oracle") ;
  static const char * const uncertain_key = "uncertain" ;
  static const char * const primary_key = "primary_candidates" ;
  static const char * const possible_key = "possible_candidates" ;

  if(!set)
  {
    oracle_p.unsetValue() ;
    return ;
  }

  QMap<QString, QVariant> map ;

  tz_suggestions_t &s = settings->cellular_zone->suggestions ;

  bool uncertain = s.gq == Uncertain ;
  if(uncertain)
  {
    map.insert(uncertain_key, true) ;
    QList<QVariant> primary_list ;
    for(vector<olson*>::iterator it=s.zones.begin(); it!=s.zones.end(); ++it)
      primary_list << string_std_to_q((*it)->name()) ;
    map.insert(primary_key, primary_list) ;
    (void) possible_key ; // not used variable
  }
  else
  {
    map.insert(uncertain_key, false) ;
  }

  oracle_p.setValue(map) ;
}

void Timed::open_epoch()
{
  am->open_epoch() ;
  time_operational_p->setValue(true) ;
}
