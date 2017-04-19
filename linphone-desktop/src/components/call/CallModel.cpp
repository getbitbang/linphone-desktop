/*
 * CallModel.cpp
 * Copyright (C) 2017  Belledonne Communications, Grenoble, France
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *  Created on: February 2, 2017
 *      Author: Ronan Abhamon
 */

#include <QDateTime>
#include <QtDebug>
#include <QTimer>

#include "../../app/App.hpp"
#include "../../Utils.hpp"
#include "../core/CoreManager.hpp"

#include "CallModel.hpp"

#define AUTO_ANSWER_OBJECT_NAME "auto-answer-timer"

using namespace std;

// =============================================================================

CallModel::CallModel (shared_ptr<linphone::Call> linphoneCall) {
  Q_ASSERT(linphoneCall != nullptr);
  mLinphoneCall = linphoneCall;
  mLinphoneCall->setData("call-model", *this);

  // Deal with auto-answer.
  {
    SettingsModel *settings = CoreManager::getInstance()->getSettingsModel();

    if (settings->getAutoAnswerStatus()) {
      QTimer *timer = new QTimer(this);
      timer->setInterval(settings->getAutoAnswerDelay());
      timer->setSingleShot(true);
      timer->setObjectName(AUTO_ANSWER_OBJECT_NAME);

      QObject::connect(timer, &QTimer::timeout, this, &CallModel::accept);
      timer->start();
    }
  }

  QObject::connect(
    &(*CoreManager::getInstance()->getHandlers()), &CoreHandlers::callStateChanged,
    this, [this](const shared_ptr<linphone::Call> &call, linphone::CallState state) {
      if (call != mLinphoneCall)
        return;

      switch (state) {
        case linphone::CallStateEnd:
        case linphone::CallStateError:
          stopAutoAnswerTimer();
          mPausedByRemote = false;
          break;

        case linphone::CallStateConnected:
        case linphone::CallStateRefered:
        case linphone::CallStateReleased:
        case linphone::CallStateStreamsRunning:
          mPausedByRemote = false;
          break;

        case linphone::CallStatePausedByRemote:
          mPausedByRemote = true;
          break;

        case linphone::CallStatePausing:
          mPausedByUser = true;
          break;

        case linphone::CallStateResuming:
          mPausedByUser = false;
          break;

        case linphone::CallStateUpdatedByRemote:
          if (
            !mLinphoneCall->getCurrentParams()->videoEnabled() &&
            mLinphoneCall->getRemoteParams()->videoEnabled()
          ) {
            mLinphoneCall->deferUpdate();
            emit videoRequested();
          }

          break;

        default:
          break;
      }

      emit statusChanged(getStatus());
    }
  );
}

CallModel::~CallModel () {
  mLinphoneCall->unsetData("call-model");
}

// -----------------------------------------------------------------------------

void CallModel::setRecordFile (shared_ptr<linphone::CallParams> &callParams) {
  callParams->setRecordFile(
    ::Utils::qStringToLinphoneString(
      CoreManager::getInstance()->getSettingsModel()->getSavedVideosFolder() +
      QDateTime::currentDateTime().toString("yyyy-MM-dd_hh:mm:ss")
    ) + ".mkv"
  );
}

void CallModel::updateStats (const shared_ptr<const linphone::CallStats> &stats) {
  switch (stats->getType()) {
    case linphone::StreamTypeAudio:
      updateStats(stats, mAudioStats);
      break;
    case linphone::StreamTypeVideo:
      updateStats(stats, mVideoStats);
      break;
    default:
      break;
  }

  emit statsUpdated();
}

// -----------------------------------------------------------------------------

void CallModel::accept () {
  stopAutoAnswerTimer();

  shared_ptr<linphone::Core> core = CoreManager::getInstance()->getCore();
  shared_ptr<linphone::CallParams> params = core->createCallParams(mLinphoneCall);
  params->enableVideo(false);
  setRecordFile(params);

  App::getInstance()->getCallsWindow()->show();
  mLinphoneCall->acceptWithParams(params);
}

void CallModel::acceptWithVideo () {
  stopAutoAnswerTimer();

  shared_ptr<linphone::Core> core = CoreManager::getInstance()->getCore();
  shared_ptr<linphone::CallParams> params = core->createCallParams(mLinphoneCall);
  params->enableVideo(true);
  setRecordFile(params);

  App::getInstance()->getCallsWindow()->show();
  mLinphoneCall->acceptWithParams(params);
}

void CallModel::terminate () {
  CoreManager *core = CoreManager::getInstance();

  core->lockVideoRender();
  mLinphoneCall->terminate();
  core->unlockVideoRender();
}

void CallModel::transfer () {
  // TODO
}

void CallModel::acceptVideoRequest () {
  shared_ptr<linphone::Core> core = CoreManager::getInstance()->getCore();
  shared_ptr<linphone::CallParams> params = core->createCallParams(mLinphoneCall);
  params->enableVideo(true);

  mLinphoneCall->acceptUpdate(params);
}

void CallModel::rejectVideoRequest () {
  mLinphoneCall->acceptUpdate(mLinphoneCall->getCurrentParams());
}

void CallModel::takeSnapshot () {
  static QString oldName;
  QString newName = QDateTime::currentDateTime().toString("yyyy-MM-dd_hh:mm:ss") + ".jpg";

  if (newName == oldName) {
    qWarning() << "Unable to take snapshot. Wait one second.";
    return;
  }

  oldName = newName;

  qInfo() << "Take snapshot of call:" << &mLinphoneCall;

  mLinphoneCall->takeVideoSnapshot(
    ::Utils::qStringToLinphoneString(
      CoreManager::getInstance()->getSettingsModel()->getSavedScreenshotsFolder() + newName
    )
  );
}

void CallModel::startRecording () {
  if (mRecording)
    return;

  qInfo() << "Start recording call:" << &mLinphoneCall;

  mLinphoneCall->startRecording();
  mRecording = true;

  emit recordingChanged(true);
}

void CallModel::stopRecording () {
  if (mRecording) {
    qInfo() << "Stop recording call:" << &mLinphoneCall;

    mRecording = false;
    mLinphoneCall->stopRecording();

    emit recordingChanged(false);
  }
}

// -----------------------------------------------------------------------------

void CallModel::stopAutoAnswerTimer () const {
  QTimer *timer = findChild<QTimer *>(AUTO_ANSWER_OBJECT_NAME, Qt::FindDirectChildrenOnly);
  if (timer) {
    timer->stop();
    timer->deleteLater();
  }
}

QString CallModel::getSipAddress () const {
  return ::Utils::linphoneStringToQString(mLinphoneCall->getRemoteAddress()->asStringUriOnly());
}

CallModel::CallStatus CallModel::getStatus () const {
  switch (mLinphoneCall->getState()) {
    case linphone::CallStateConnected:
    case linphone::CallStateStreamsRunning:
      return CallStatusConnected;

    case linphone::CallStateEnd:
    case linphone::CallStateError:
    case linphone::CallStateRefered:
    case linphone::CallStateReleased:
      return CallStatusEnded;

    case linphone::CallStatePaused:
    case linphone::CallStatePausedByRemote:
    case linphone::CallStatePausing:
    case linphone::CallStateResuming:
      return CallStatusPaused;

    case linphone::CallStateUpdating:
    case linphone::CallStateUpdatedByRemote:
      return mPausedByRemote ? CallStatusPaused : CallStatusConnected;

    case linphone::CallStateEarlyUpdatedByRemote:
    case linphone::CallStateEarlyUpdating:
    case linphone::CallStateIdle:
    case linphone::CallStateIncomingEarlyMedia:
    case linphone::CallStateIncomingReceived:
    case linphone::CallStateOutgoingEarlyMedia:
    case linphone::CallStateOutgoingInit:
    case linphone::CallStateOutgoingProgress:
    case linphone::CallStateOutgoingRinging:
      break;
  }

  return mLinphoneCall->getDir() == linphone::CallDirIncoming ? CallStatusIncoming : CallStatusOutgoing;
}

// -----------------------------------------------------------------------------

int CallModel::getDuration () const {
  return mLinphoneCall->getDuration();
}

float CallModel::getQuality () const {
  return mLinphoneCall->getCurrentQuality();
}

// -----------------------------------------------------------------------------

#define VU_MIN (-20.f)
#define VU_MAX (4.f)

inline float computeVu (float volume) {
  if (volume < VU_MIN)
    return 0.f;
  if (volume > VU_MAX)
    return 1.f;

  return (volume - VU_MIN) / (VU_MAX - VU_MIN);
}

float CallModel::getMicroVu () const {
  return computeVu(mLinphoneCall->getRecordVolume());
}

float CallModel::getSpeakerVu () const {
  return computeVu(mLinphoneCall->getPlayVolume());
}

// -----------------------------------------------------------------------------

bool CallModel::getMicroMuted () const {
  return !CoreManager::getInstance()->getCore()->micEnabled();
}

void CallModel::setMicroMuted (bool status) {
  shared_ptr<linphone::Core> core = CoreManager::getInstance()->getCore();

  if (status == core->micEnabled()) {
    core->enableMic(!status);
    emit microMutedChanged(status);
  }
}

// -----------------------------------------------------------------------------

bool CallModel::getPausedByUser () const {
  return mPausedByUser;
}

void CallModel::setPausedByUser (bool status) {
  switch (mLinphoneCall->getState()) {
    case linphone::CallStateConnected:
    case linphone::CallStateStreamsRunning:
    case linphone::CallStatePaused:
    case linphone::CallStatePausedByRemote:
      break;
    default: return;
  }

  if (status) {
    if (!mPausedByUser)
      mLinphoneCall->pause();

    return;
  }

  if (mPausedByUser)
    mLinphoneCall->resume();
}

// -----------------------------------------------------------------------------

bool CallModel::getVideoEnabled () const {
  shared_ptr<const linphone::CallParams> params = mLinphoneCall->getCurrentParams();
  return params && params->videoEnabled() && getStatus() == CallStatusConnected;
}

void CallModel::setVideoEnabled (bool status) {
  switch (mLinphoneCall->getState()) {
    case linphone::CallStateConnected:
    case linphone::CallStateStreamsRunning:
      break;
    default: return;
  }

  if (status == getVideoEnabled())
    return;

  shared_ptr<linphone::Core> core = CoreManager::getInstance()->getCore();
  shared_ptr<linphone::CallParams> params = core->createCallParams(mLinphoneCall);
  params->enableVideo(status);

  mLinphoneCall->update(params);
}

// -----------------------------------------------------------------------------

bool CallModel::getUpdating () const {
  switch (mLinphoneCall->getState()) {
    case linphone::CallStateConnected:
    case linphone::CallStateStreamsRunning:
    case linphone::CallStatePaused:
    case linphone::CallStatePausedByRemote:
      return false;

    default:
      break;
  }

  return true;
}

bool CallModel::getRecording () const {
  return mRecording;
}

// -----------------------------------------------------------------------------

void CallModel::sendDtmf (const QString &dtmf) {
  qInfo() << QStringLiteral("Send dtmf: `%1`.").arg(dtmf);
  mLinphoneCall->sendDtmf(dtmf.constData()[0].toLatin1());
}

// -----------------------------------------------------------------------------

QVariantList CallModel::getAudioStats () const {
  return mAudioStats;
}

QVariantList CallModel::getVideoStats () const {
  return mVideoStats;
}

inline QVariantMap createStat (const QString &key, const QString &value) {
  QVariantMap m;
  m["key"] = key;
  m["value"] = value;
  return m;
}

void CallModel::updateStats (const shared_ptr<const linphone::CallStats> &callStats, QVariantList &stats) {
  QString family;
  shared_ptr<const linphone::CallParams> params = mLinphoneCall->getCurrentParams();
  shared_ptr<const linphone::PayloadType> payloadType;

  switch (callStats->getType()) {
    case linphone::StreamTypeAudio:
      payloadType = params->getUsedAudioPayloadType();
      break;
    case linphone::StreamTypeVideo:
      payloadType = params->getUsedVideoPayloadType();
      break;
    default:
      return;
  }

  switch (callStats->getIpFamilyOfRemote()) {
    case linphone::AddressFamilyInet:
      family = "IPv4";
      break;
    case linphone::AddressFamilyInet6:
      family = "IPv6";
      break;
    default:
      family = "Unknown";
      break;
  }

  stats.clear();

  stats << createStat(tr("callStatsCodec"), payloadType
    ? QString("%1 / %2kHz").arg(Utils::linphoneStringToQString(payloadType->getMimeType())).arg(payloadType->getClockRate() / 1000)
    : "");
  stats << createStat(tr("callStatsUploadBandwidth"), QString("%1 kbits/s").arg(int(callStats->getUploadBandwidth())));
  stats << createStat(tr("callStatsDownloadBandwidth"), QString("%1 kbits/s").arg(int(callStats->getDownloadBandwidth())));
  stats << createStat(tr("callStatsIceState"), iceStateToString(callStats->getIceState()));
  stats << createStat(tr("callStatsIpFamily"), family);
  stats << createStat(tr("callStatsSenderLossRate"), QString("%1 %").arg(callStats->getSenderLossRate()));
  stats << createStat(tr("callStatsReceiverLossRate"), QString("%1 %").arg(callStats->getReceiverLossRate()));

  switch (callStats->getType()) {
    case linphone::StreamTypeAudio:
      stats << createStat(tr("callStatsJitterBuffer"), QString("%1 ms").arg(callStats->getJitterBufferSizeMs()));
      break;
    case linphone::StreamTypeVideo: {
      QString sentVideoDefinitionName = Utils::linphoneStringToQString(params->getSentVideoDefinition()->getName());
      QString sentVideoDefinition = QString("%1x%2")
        .arg(params->getSentVideoDefinition()->getWidth())
        .arg(params->getSentVideoDefinition()->getHeight());

      stats << createStat(tr("callStatsSentVideoDefinition"), sentVideoDefinition == sentVideoDefinitionName
        ? sentVideoDefinition
        : QString("%1 (%2)").arg(sentVideoDefinition).arg(sentVideoDefinitionName));

      QString receivedVideoDefinitionName = Utils::linphoneStringToQString(params->getReceivedVideoDefinition()->getName());
      QString receivedVideoDefinition = QString("%1x%2")
        .arg(params->getReceivedVideoDefinition()->getWidth())
        .arg(params->getReceivedVideoDefinition()->getHeight());

      stats << createStat(tr("callStatsReceivedVideoDefinition"), receivedVideoDefinition == receivedVideoDefinitionName
        ? receivedVideoDefinition
        : QString("%1 (%2)").arg(receivedVideoDefinition).arg(receivedVideoDefinitionName));
    }
    break;
    default:
      break;
  }
}

QString CallModel::iceStateToString (linphone::IceState state) const {
  switch (state) {
    case linphone::IceStateNotActivated:
      return tr("iceStateNotActivated");
    case linphone::IceStateFailed:
      return tr("iceStateFailed");
    case linphone::IceStateInProgress:
      return tr("iceStateInProgress");
    case linphone::IceStateReflexiveConnection:
      return tr("iceStateReflexiveConnection");
    case linphone::IceStateHostConnection:
      return tr("iceStateHostConnection");
    case linphone::IceStateRelayConnection:
      return tr("iceStateRelayConnection");
    default:
      return tr("iceStateInvalid");
  }
}
