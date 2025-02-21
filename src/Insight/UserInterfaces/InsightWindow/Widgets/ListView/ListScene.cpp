#include "ListScene.hpp"

#include <QMenu>
#include <QApplication>
#include <QClipboard>
#include <QByteArray>
#include <algorithm>

namespace Bloom::Widgets
{
    ListScene::ListScene(
        ListScene::ListItemSetType&& items,
        QGraphicsView* parent
    )
        : parent(parent)
        , QGraphicsScene(parent)
    {
        this->setItemIndexMethod(QGraphicsScene::NoIndex);
        this->setItems(std::move(items));
    }

    void ListScene::setSelectionLimit(std::uint8_t selectionLimit) {
        this->selectionLimit = selectionLimit;
    }

    void ListScene::refreshGeometry() {
        const auto* viewport = this->viewport();
        const auto viewportWidth = viewport != nullptr ? viewport->width() : 0;
        const auto viewportHeight = viewport != nullptr ? viewport->height() : 0;

        const auto startXPosition = this->margins.left();
        auto startYPosition = this->margins.top();

        for (auto& listItem : this->listItems) {
            if (!listItem->isVisible()) {
                continue;
            }

            listItem->size.setWidth(viewportWidth - this->margins.left() - this->margins.right());
            listItem->setPos(startXPosition, startYPosition);

            listItem->onGeometryChanged();

            startYPosition += listItem->size.height();
        }

        this->setSceneRect(0, 0, viewportWidth, std::max(viewportHeight, startYPosition));
        this->update();
    }

    void ListScene::setItems(const ListScene::ListItemSetType& items) {
        for (auto& item : this->items()) {
            this->removeItem(item);
        }

        this->listItems = items;

        for (const auto& listItem : this->listItems) {
            this->addItem(listItem);
        }
    }

    void ListScene::addListItem(ListItem* item) {
        this->listItems.insert(item);
        this->addItem(item);
    }

    void ListScene::removeListItem(ListItem* item) {
        this->listItems.erase(item);
        this->removeItem(item);
    }

    void ListScene::setEnabled(bool enabled) {
        if (this->enabled == enabled) {
            return;
        }

        this->enabled = enabled;

        for (auto& item : this->listItems) {
            item->setEnabled(this->enabled);
        }

        this->update();
    }

    void ListScene::mousePressEvent(QGraphicsSceneMouseEvent* mouseEvent) {
        const auto button = mouseEvent->button();

        const auto mousePosition = mouseEvent->buttonDownScenePos(button);

        const auto items = this->items(mousePosition);
        if (items.empty()) {
            return;
        }

        auto* clickedListItem = dynamic_cast<ListItem*>(items.first());
        if (clickedListItem == nullptr) {
            return;
        }

        if (clickedListItem->selected && button == Qt::MouseButton::RightButton) {
            return;
        }

        const auto selectedItemCount = this->selectedItems.size();
        if (selectedItemCount > 0) {
            const auto ctrlModifierEnabled = (mouseEvent->modifiers() & Qt::ControlModifier) != 0;
            if (!ctrlModifierEnabled || selectedItemCount >= this->selectionLimit) {
                const auto itemsToRemove = ctrlModifierEnabled
                    ? selectedItemCount - this->selectionLimit + 1
                    : selectedItemCount;

                auto itemIt = this->selectedItems.begin();
                while (
                    itemIt != this->selectedItems.end()
                    && (selectedItemCount - this->selectedItems.size()) < itemsToRemove
                ) {
                    auto& item = *itemIt;
                    item->selected = false;
                    item->update();

                    this->selectedItems.erase(itemIt++);
                }
            }
        }

        if (this->selectionLimit > 0) {
            this->selectedItems.push_back(clickedListItem);
            clickedListItem->selected = true;
            clickedListItem->update();
            emit this->selectionChanged(this->selectedItems);
        }

        emit this->itemClicked(clickedListItem);
    }

    void ListScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* mouseEvent) {
        if (mouseEvent->button() != Qt::MouseButton::LeftButton) {
            return;
        }

        const auto mousePosition = mouseEvent->buttonDownScenePos(Qt::MouseButton::LeftButton);

        const auto items = this->items(mousePosition);
        if (items.empty()) {
            return;
        }

        auto* clickedListItem = dynamic_cast<ListItem*>(items.first());
        if (clickedListItem == nullptr) {
            return;
        }

        if (!clickedListItem->selected) {
            /*
             * Sometimes, QT won't trigger a press event when the user double clicks. This usually happens when the
             * first click closes a context menu.
             */
            this->mousePressEvent(mouseEvent);
        }

        emit this->itemDoubleClicked(clickedListItem);
    }

    void ListScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
        const auto items = this->items(event->scenePos());

        if (items.empty()) {
            return;
        }

        auto* listItem = dynamic_cast<ListItem*>(items.first());
        if (listItem == nullptr) {
            return;
        }

        emit this->itemContextMenu(listItem, event->screenPos());
    }
}
