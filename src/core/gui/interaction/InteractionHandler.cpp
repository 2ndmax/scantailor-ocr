// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "InteractionHandler.h"

#include <QKeyEvent>
#include <QPainter>
#include <cassert>

#include "InteractionState.h"

struct InteractionHandler::HandlerList {
  using Nodes = boost::intrusive::list<InteractionHandler, boost::intrusive::constant_time_size<false>>;

  struct Cursor {
    HandlerList* list = nullptr;
    Nodes::iterator it;
    Cursor* prev = nullptr;
    Cursor* nextCursor = nullptr;

    explicit Cursor(HandlerList& owner) : list(&owner), it(owner.nodes.begin()), nextCursor(owner.activeCursors) {
      if (nextCursor) {
        nextCursor->prev = this;
      }
      list->activeCursors = this;
    }

    ~Cursor() {
      if (prev) {
        prev->nextCursor = nextCursor;
      } else if (list) {
        list->activeCursors = nextCursor;
      }
      if (nextCursor) {
        nextCursor->prev = prev;
      }
    }

    Cursor(const Cursor&) = delete;
    Cursor& operator=(const Cursor&) = delete;

    bool atEnd() const { return !list || (it == list->nodes.end()); }

    InteractionHandler& next() {
      InteractionHandler& handler = *it;
      ++it;
      return handler;
    }
  };

  void notifyRemoving(const InteractionHandler* const node) {
    for (Cursor* c = activeCursors; c != nullptr; c = c->nextCursor) {
      if ((c->it != nodes.end()) && (&(*c->it) == node)) {
        ++c->it;
      }
    }
  }

  void invalidateAllCursors() {
    for (Cursor* c = activeCursors; c != nullptr; c = c->nextCursor) {
      c->it = nodes.end();
    }
  }

  Nodes nodes;
  Cursor* activeCursors = nullptr;
};

#define DISPATCH(list, call)                                    \
  {                                                             \
    for (HandlerList::Cursor cursor(*list); !cursor.atEnd();) { \
      cursor.next().call;                                       \
    }                                                           \
  }

#define RETURN_IF_ACCEPTED(event) \
  {                               \
    if (event->isAccepted()) {    \
      return;                     \
    }                             \
  }

namespace {
class ScopedClearAcceptance {
  DECLARE_NON_COPYABLE(ScopedClearAcceptance)

 public:
  explicit ScopedClearAcceptance(QEvent* event);

  ~ScopedClearAcceptance();

 private:
  QEvent* m_event;
  bool m_wasAccepted;
};


ScopedClearAcceptance::ScopedClearAcceptance(QEvent* event) : m_event(event), m_wasAccepted(event->isAccepted()) {
  m_event->setAccepted(false);
}

ScopedClearAcceptance::~ScopedClearAcceptance() {
  if (m_wasAccepted) {
    m_event->setAccepted(true);
  }
}
}  // anonymous namespace

InteractionHandler::InteractionHandler()
    : m_preceeders(std::make_shared<HandlerList>()), m_followers(std::make_shared<HandlerList>()) {}

InteractionHandler::~InteractionHandler() {
  unlink();
  m_preceeders->invalidateAllCursors();
  m_followers->invalidateAllCursors();
  const auto deleteHandler = [](InteractionHandler* handler) {
    handler->m_parentList = nullptr;
    delete handler;
  };
  m_preceeders->nodes.clear_and_dispose(deleteHandler);
  m_followers->nodes.clear_and_dispose(deleteHandler);
}

void InteractionHandler::unlink() {
  if (m_parentList) {
    m_parentList->notifyRemoving(this);
    m_parentList = nullptr;
  }
  Hook::unlink();
}

void InteractionHandler::paint(QPainter& painter, const InteractionState& interaction) {
  // Keep them alive in case this object gets destroyed.
  std::shared_ptr<HandlerList> preceeders(m_preceeders);
  std::shared_ptr<HandlerList> followers(m_followers);

  DISPATCH(preceeders, paint(painter, interaction));
  painter.save();
  onPaint(painter, interaction);
  painter.restore();
  DISPATCH(followers, paint(painter, interaction));
}

void InteractionHandler::proximityUpdate(const QPointF& screenMousePos, InteractionState& interaction) {
  // Keep them alive in case this object gets destroyed.
  std::shared_ptr<HandlerList> preceeders(m_preceeders);
  std::shared_ptr<HandlerList> followers(m_followers);

  DISPATCH(preceeders, proximityUpdate(screenMousePos, interaction));
  onProximityUpdate(screenMousePos, interaction);
  assert(!interaction.captured() && "onProximityUpdate() must not capture interaction");
  DISPATCH(followers, proximityUpdate(screenMousePos, interaction));
}

void InteractionHandler::keyPressEvent(QKeyEvent* event, InteractionState& interaction) {
  RETURN_IF_ACCEPTED(event);

  // Keep them alive in case this object gets destroyed.
  std::shared_ptr<HandlerList> preceeders(m_preceeders);
  std::shared_ptr<HandlerList> followers(m_followers);

  DISPATCH(preceeders, keyPressEvent(event, interaction));
  RETURN_IF_ACCEPTED(event);
  onKeyPressEvent(event, interaction);
  ScopedClearAcceptance guard(event);
  DISPATCH(followers, keyPressEvent(event, interaction));
}

void InteractionHandler::keyReleaseEvent(QKeyEvent* event, InteractionState& interaction) {
  RETURN_IF_ACCEPTED(event);
  // Keep them alive in case this object gets destroyed.
  std::shared_ptr<HandlerList> preceeders(m_preceeders);
  std::shared_ptr<HandlerList> followers(m_followers);

  DISPATCH(preceeders, keyReleaseEvent(event, interaction));
  RETURN_IF_ACCEPTED(event);
  onKeyReleaseEvent(event, interaction);
  ScopedClearAcceptance guard(event);
  DISPATCH(followers, keyReleaseEvent(event, interaction));
}

void InteractionHandler::mousePressEvent(QMouseEvent* event, InteractionState& interaction) {
  RETURN_IF_ACCEPTED(event);

  // Keep them alive in case this object gets destroyed.
  std::shared_ptr<HandlerList> preceeders(m_preceeders);
  std::shared_ptr<HandlerList> followers(m_followers);

  DISPATCH(preceeders, mousePressEvent(event, interaction));
  RETURN_IF_ACCEPTED(event);
  onMousePressEvent(event, interaction);
  ScopedClearAcceptance guard(event);
  DISPATCH(followers, mousePressEvent(event, interaction));
}

void InteractionHandler::mouseReleaseEvent(QMouseEvent* event, InteractionState& interaction) {
  RETURN_IF_ACCEPTED(event);
  // Keep them alive in case this object gets destroyed.
  std::shared_ptr<HandlerList> preceeders(m_preceeders);
  std::shared_ptr<HandlerList> followers(m_followers);

  DISPATCH(preceeders, mouseReleaseEvent(event, interaction));
  RETURN_IF_ACCEPTED(event);
  onMouseReleaseEvent(event, interaction);
  ScopedClearAcceptance guard(event);
  DISPATCH(followers, mouseReleaseEvent(event, interaction));
}

void InteractionHandler::mouseDoubleClickEvent(QMouseEvent* event, InteractionState& interaction) {
  RETURN_IF_ACCEPTED(event);
  // Keep them alive in case this object gets destroyed.
  std::shared_ptr<HandlerList> preceeders(m_preceeders);
  std::shared_ptr<HandlerList> followers(m_followers);

  DISPATCH(preceeders, mouseDoubleClickEvent(event, interaction));
  RETURN_IF_ACCEPTED(event);
  onMouseDoubleClickEvent(event, interaction);
  ScopedClearAcceptance guard(event);
  DISPATCH(followers, mouseDoubleClickEvent(event, interaction));
}

void InteractionHandler::mouseMoveEvent(QMouseEvent* event, InteractionState& interaction) {
  RETURN_IF_ACCEPTED(event);

  // Keep them alive in case this object gets destroyed.
  std::shared_ptr<HandlerList> preceeders(m_preceeders);
  std::shared_ptr<HandlerList> followers(m_followers);

  DISPATCH(preceeders, mouseMoveEvent(event, interaction));
  RETURN_IF_ACCEPTED(event);
  onMouseMoveEvent(event, interaction);
  ScopedClearAcceptance guard(event);
  DISPATCH(followers, mouseMoveEvent(event, interaction));
}

void InteractionHandler::wheelEvent(QWheelEvent* event, InteractionState& interaction) {
  RETURN_IF_ACCEPTED(event);

  // Keep them alive in case this object gets destroyed.
  std::shared_ptr<HandlerList> preceeders(m_preceeders);
  std::shared_ptr<HandlerList> followers(m_followers);

  DISPATCH(preceeders, wheelEvent(event, interaction));
  RETURN_IF_ACCEPTED(event);
  onWheelEvent(event, interaction);
  ScopedClearAcceptance guard(event);
  DISPATCH(followers, wheelEvent(event, interaction));
}

void InteractionHandler::contextMenuEvent(QContextMenuEvent* event, InteractionState& interaction) {
  RETURN_IF_ACCEPTED(event);
  // Keep them alive in case this object gets destroyed.
  std::shared_ptr<HandlerList> preceeders(m_preceeders);
  std::shared_ptr<HandlerList> followers(m_followers);

  DISPATCH(preceeders, contextMenuEvent(event, interaction));
  RETURN_IF_ACCEPTED(event);
  onContextMenuEvent(event, interaction);
  ScopedClearAcceptance guard(event);
  DISPATCH(followers, contextMenuEvent(event, interaction));
}

void InteractionHandler::makePeerPreceeder(InteractionHandler& handler) {
  handler.unlink();
  handler.m_parentList = m_parentList;
  HandlerList::Nodes::node_algorithms::link_before(this, &handler);
}

void InteractionHandler::makePeerFollower(InteractionHandler& handler) {
  handler.unlink();
  handler.m_parentList = m_parentList;
  HandlerList::Nodes::node_algorithms::link_after(this, &handler);
}

void InteractionHandler::makeFirstPreceeder(InteractionHandler& handler) {
  handler.unlink();
  handler.m_parentList = m_preceeders.get();
  m_preceeders->nodes.push_front(handler);
}

void InteractionHandler::makeLastPreceeder(InteractionHandler& handler) {
  handler.unlink();
  handler.m_parentList = m_preceeders.get();
  m_preceeders->nodes.push_back(handler);
}

void InteractionHandler::makeFirstFollower(InteractionHandler& handler) {
  handler.unlink();
  handler.m_parentList = m_followers.get();
  m_followers->nodes.push_front(handler);
}

void InteractionHandler::makeLastFollower(InteractionHandler& handler) {
  handler.unlink();
  handler.m_parentList = m_followers.get();
  m_followers->nodes.push_back(handler);
}

bool InteractionHandler::defaultInteractionPermitter(const InteractionState& interaction) {
  return !interaction.captured();
}
