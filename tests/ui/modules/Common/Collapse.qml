import QtQuick 2.7

import Common.Styles 1.0

// ===================================================================
// A simple component to build collapsed item.
// ===================================================================

Item {
  property bool _collapsed: false

  signal collapsed (bool collapsed)

  // -----------------------------------------------------------------

  function collapse () {
    _collapsed = !_collapsed
  }

  function isCollapsed () {
    return _collapsed
  }

  // -----------------------------------------------------------------

  ActionButton {
    id: button

    anchors.centerIn: parent
    background: CollapseStyle.background
    icon: 'collapse'
    iconSize: CollapseStyle.iconSize

    onClicked: collapse()
  }

  // -----------------------------------------------------------------

  states: [
    State {
      name: 'Collapsed'
      when: _collapsed

      PropertyChanges {
        target: button
        rotation: 180
      }
    }
  ]

  transitions: Transition {
    RotationAnimation {
      direction: RotationAnimation.Clockwise
      duration: CollapseStyle.animationDuration
      property: 'rotation'
      target: button
    }

    SequentialAnimation {
      PauseAnimation {
        duration: CollapseStyle.animationDuration
      }

      ScriptAction {
        script: collapsed(_collapsed)
      }
    }
  }
}
