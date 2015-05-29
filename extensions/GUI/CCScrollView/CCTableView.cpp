/****************************************************************************
 Copyright (c) 2012 cocos2d-x.org
 Copyright (c) 2010 Sangwoo Im

 http://www.cocos2d-x.org

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/

#include "CCTableView.h"
#include "CCTableViewCell.h"

NS_CC_EXT_BEGIN

#define DEL_SIGN_WIDTH 200
#define DEL_SIGN_NODE_TAG 743
#define DEL_SIGN_MOVE_TIME 0.2f

TableView* TableView::create()
{
    return TableView::create(nullptr, Size::ZERO);
}

TableView* TableView::create(TableViewDataSource* dataSource, Size size)
{
    return TableView::create(dataSource, size, nullptr);
}

TableView* TableView::create(TableViewDataSource* dataSource,
    Size size,
    Node* container)
{
    TableView* table = new (std::nothrow) TableView();
    table->initWithViewSize(size, container);
    table->autorelease();
    table->setDataSource(dataSource);
    table->_updateCellPositions();
    table->_updateContentSize();

    return table;
}
void TableView::scrolltoCell(int idx, bool vertical, bool animation)
{
    float offset = 0;
    for (auto i = _dataSource->numberOfCellsInTableView(this) - 1; i >= idx;
         --i) {
        offset += (vertical ? _dataSource->tableCellSizeForIndex(this, i).height
                            : _dataSource->tableCellSizeForIndex(this, i).width);
    }
    if (vertical) {
        offset = getViewSize().height - offset;
        if (offset > 0)
            offset = 0;
        else if (offset < -getContentSize().height)
            offset = getContentSize().height;

        setContentOffsetInDuration({ 0, offset }, animation ? 1 : 0);
    }
    else {
        offset = getViewSize().width - offset;
        if (offset > 0)
            offset = 0;
        else if (offset < -getContentSize().width)
            offset = getContentSize().width;

        setContentOffsetInDuration({ offset, 0 }, animation ? 1 : 0);
    }
}
bool TableView::initWithViewSize(Size size, Node* container /* = nullptr*/)
{
    if (ScrollView::initWithViewSize(size, container)) {
        CC_SAFE_DELETE(_indices);
        _indices = new std::set<ssize_t>();
        _vordering = VerticalFillOrder::BOTTOM_UP;
        this->setDirection(Direction::VERTICAL);

        ScrollView::setDelegate(this);

        return true;
    }
    return false;
}

TableView::TableView()
    : _touchedCell(nullptr)
    , _indices(nullptr)
    , _dataSource(nullptr)
    , _tableViewDelegate(nullptr)
    , _oldDirection(Direction::NONE)
    , _isUsedCellsDirty(false)
    , _del_cell_status(false)
    , _cell_move_direction(Direction::NONE)
    , _show_del_state(DEL_CELL_MOVE_STATE::NONE)
    , _touchedShowDelCell(nullptr)
{
}

TableView::~TableView()
{
    CC_SAFE_DELETE(_indices);
}

void TableView::setVerticalFillOrder(VerticalFillOrder fillOrder)
{
    if (_vordering != fillOrder) {
        _vordering = fillOrder;
        if (!_cellsUsed.empty()) {
            this->reloadData();
        }
    }
}

TableView::VerticalFillOrder TableView::getVerticalFillOrder()
{
    return _vordering;
}

void TableView::reloadData()
{
    getContainer()->stopAllActions();
    _oldDirection = Direction::NONE;

    for (auto cell : _cellsUsed) {
        if (_tableViewDelegate != nullptr) {
            _tableViewDelegate->tableCellWillRecycle(this, cell);
        }

        _cellsFreed[cell->getTag()].pushBack(cell);

        cell->reset();
        if (cell->getParent() == this->getContainer()) {
            this->getContainer()->removeChild(cell, true);
        }
    }

    _indices->clear();
    _cellsUsed.clear();

    this->_updateCellPositions();
    this->_updateContentSize();
    if (_dataSource->numberOfCellsInTableView(this) > 0) {
        this->scrollViewDidScroll(this);
    }
}

TableViewCell* TableView::cellAtIndex(ssize_t idx)
{
    if (_indices->find(idx) != _indices->end()) {
        for (auto cell : _cellsUsed) {
            if (cell->getIdx() == idx) {
                return cell;
            }
        }
    }

    return nullptr;
}

void TableView::updateCellAtIndex(ssize_t idx)
{
    if (idx == CC_INVALID_INDEX) {
        return;
    }
    long countOfItems = _dataSource->numberOfCellsInTableView(this);
    if (0 == countOfItems || idx > countOfItems - 1) {
        return;
    }

    TableViewCell* cell = this->cellAtIndex(idx);
    if (cell) {
        this->_moveCellOutOfSight(cell);
    }
    cell = _dataSource->tableCellAtIndex(this, idx);
    this->_setIndexForCell(idx, cell);
    this->_addCellIfNecessary(cell);
}

void TableView::insertCellAtIndex(ssize_t idx)
{
    if (idx == CC_INVALID_INDEX) {
        return;
    }

    long countOfItems = _dataSource->numberOfCellsInTableView(this);
    if (0 == countOfItems || idx > countOfItems - 1) {
        return;
    }

    long newIdx = 0;

    auto cell = cellAtIndex(idx);
    if (cell) {
        newIdx = _cellsUsed.getIndex(cell);
        // Move all cells behind the inserted position
        for (long i = newIdx; i < _cellsUsed.size(); i++) {
            cell = _cellsUsed.at(i);
            this->_setIndexForCell(cell->getIdx() + 1, cell);
        }
    }

    // insert a new cell
    cell = _dataSource->tableCellAtIndex(this, idx);
    this->_setIndexForCell(idx, cell);
    this->_addCellIfNecessary(cell);

    this->_updateCellPositions();
    this->_updateContentSize();
}

void TableView::removeCellAtIndex(ssize_t idx)
{
    if (idx == CC_INVALID_INDEX) {
        return;
    }

    long uCountOfItems = _dataSource->numberOfCellsInTableView(this);
    if (0 == uCountOfItems || idx > uCountOfItems - 1) {
        return;
    }

    ssize_t newIdx = 0;

    TableViewCell* cell = this->cellAtIndex(idx);
    if (!cell) {
        return;
    }

    newIdx = _cellsUsed.getIndex(cell);

    // remove first
    this->_moveCellOutOfSight(cell);

    _indices->erase(idx);
    this->_updateCellPositions();

    for (ssize_t i = _cellsUsed.size() - 1; i > newIdx; i--) {
        cell = _cellsUsed.at(i);
        this->_setIndexForCell(cell->getIdx() - 1, cell);
    }
}

TableViewCell* TableView::dequeueCell(int tag)
{
    if (_cellsFreed.empty()) {
        return NULL;
    }
    else {
        for (auto ite = _cellsFreed[tag].begin(); ite != _cellsFreed[tag].end();
             ++ite) {
            if ((*ite)->getTag() == tag) {
                TableViewCell* cell = *ite;
                cell->retain();
                _cellsFreed[tag].erase(ite);
                cell->autorelease();
                return cell;
            }
        }
    }
    return NULL;
}

void TableView::_addCellIfNecessary(TableViewCell* cell)
{
    if (cell->getParent() != this->getContainer()) {
        this->getContainer()->addChild(cell);
    }
    auto size = cell->getContentSize();
    if (_del_cell_status && !cell->getChildByTag(DEL_SIGN_NODE_TAG)) {
        Node* baseNode = Node::create();
        baseNode->setPosition(Vec2(size.width, 0));
        cell->addChild(baseNode);
        baseNode->setTag(DEL_SIGN_NODE_TAG);
        
        ui::Button* b = ui::Button::create("image/3x3.jpg", "image/3x3.jpg", "image/3x3.jpg", ui::Button::TextureResType::PLIST);
        b->ignoreContentAdaptWithSize(false);
        b->setScale9Enabled(true);
        b->setColor(Color3B::RED);
        b->setContentSize({DEL_SIGN_WIDTH, size.height});
        std::shared_ptr<bool> moved = std::shared_ptr<bool>(new bool);
        b->addTouchEventListener([this, moved](Ref*, ui::Widget::TouchEventType type) {
            
            switch (type) {
                case ui::Widget::TouchEventType::BEGAN: {
                    *moved = false;
                } break;
                    
                case ui::Widget::TouchEventType::MOVED: {
                    *moved = true;
                } break;
                    
                case ui::Widget::TouchEventType::ENDED: {
                    if (!(*moved)) {
                        if (_touchedShowDelCell)
                            _delCell(_touchedShowDelCell);
                            _touchedShowDelCell = nullptr;
                            _touchedCell = nullptr;
                            _cell_move_direction = Direction::NONE;
                    }
                } break;
            }
        });
        
        baseNode->addChild(b);
        b->setAnchorPoint(Vec2(0, 0));
        b->setPosition(Vec2::ZERO);
        b->setSwallowTouches(true);
        
        Sprite* del_sign = Sprite::createWithSpriteFrameName("image/trash.png");
        del_sign->setPosition(Vec2(DEL_SIGN_WIDTH / 2, size.height / 2));
        baseNode->addChild(del_sign);
    }

    _cellsUsed.pushBack(cell);
    _indices->insert(cell->getIdx());
    _isUsedCellsDirty = true;
}

void TableView::_updateContentSize()
{
    Size size = Size::ZERO;
    ssize_t cellsCount = _dataSource->numberOfCellsInTableView(this);

    if (cellsCount > 0) {
        float maxPosition = _vCellsPositions[cellsCount];

        switch (this->getDirection()) {
        case Direction::HORIZONTAL:
            size = Size(maxPosition, _viewSize.height);
            break;
        default:
            size = Size(_viewSize.width, maxPosition);
            break;
        }
    }

    this->setContentSize(size);

    if (_oldDirection != _direction) {
        if (_direction == Direction::HORIZONTAL) {
            this->setContentOffset(Vec2(0, 0));
        }
        else {
            this->setContentOffset(Vec2(0, this->minContainerOffset().y));
        }
        _oldDirection = _direction;
    }

    if (_loading_status && _loading_node) {
        if (size.width == 0)
            size.width = _viewSize.width;
        _loading_node->setPosition(Vec2(size.width / 2, size.height + 100));
    }
}

Vec2 TableView::_offsetFromIndex(ssize_t index)
{
    Vec2 offset = this->__offsetFromIndex(index);

    const Size cellSize = _dataSource->tableCellSizeForIndex(this, index);
    if (_vordering == VerticalFillOrder::TOP_DOWN) {
        offset.y = this->getContainer()->getContentSize().height - offset.y - cellSize.height;
    }
    return offset;
}

Vec2 TableView::__offsetFromIndex(ssize_t index)
{
    Vec2 offset;
    Size cellSize;

    switch (this->getDirection()) {
    case Direction::HORIZONTAL:
        offset.set(_vCellsPositions[index], 0.0f);
        break;
    default:
        offset.set(0.0f, _vCellsPositions[index]);
        break;
    }

    return offset;
}

long TableView::_indexFromOffset(Vec2 offset)
{
    long index = 0;
    const long maxIdx = _dataSource->numberOfCellsInTableView(this) - 1;

    if (_vordering == VerticalFillOrder::TOP_DOWN) {
        offset.y = this->getContainer()->getContentSize().height - offset.y;
    }
    index = this->__indexFromOffset(offset);
    if (index != -1) {
        index = MAX(0, index);
        if (index > maxIdx) {
            index = CC_INVALID_INDEX;
        }
    }

    return index;
}

long TableView::__indexFromOffset(Vec2 offset)
{
    long low = 0;
    long high = _dataSource->numberOfCellsInTableView(this) - 1;
    float search;
    switch (this->getDirection()) {
    case Direction::HORIZONTAL:
        search = offset.x;
        break;
    default:
        search = offset.y;
        break;
    }

    while (high >= low) {
        long index = low + (high - low) / 2;
        float cellStart = _vCellsPositions[index];
        float cellEnd = _vCellsPositions[index + 1];

        if (search >= cellStart && search <= cellEnd) {
            return index;
        }
        else if (search < cellStart) {
            high = index - 1;
        }
        else {
            low = index + 1;
        }
    }

    if (low <= 0) {
        return 0;
    }

    return -1;
}

void TableView::_moveCellOutOfSight(TableViewCell* cell)
{
    if (_tableViewDelegate != nullptr) {
        _tableViewDelegate->tableCellWillRecycle(this, cell);
    }

    _cellsFreed[cell->getTag()].pushBack(cell);
    _cellsUsed.eraseObject(cell);
    _isUsedCellsDirty = true;

    _indices->erase(cell->getIdx());
    cell->reset();

    if (cell->getParent() == this->getContainer()) {
        this->getContainer()->removeChild(cell, true);
        ;
    }
}
void TableView::setDelCellStatus(bool status)
{
    _del_cell_status = status;
}

void TableView::_delCell(TableViewCell* cell)
{
    ssize_t index = cell->getIdx();
    if (_tableViewDelegate != nullptr) {
        _tableViewDelegate->tableCellWillDel(this, index);
    }
//    removeCellAtIndex(index);
    reloadData();
}

void TableView::_setIndexForCell(ssize_t index, TableViewCell* cell)
{
    cell->setAnchorPoint(Vec2(0.0f, 0.0f));
    cell->setPosition(this->_offsetFromIndex(index));
    cell->setIdx(index);
}

void TableView::_updateCellPositions()
{
    long cellsCount = _dataSource->numberOfCellsInTableView(this);
    _vCellsPositions.resize(cellsCount + 1, 0.0);

    if (cellsCount > 0) {
        float currentPos = 0;
        Size cellSize;
        for (int i = 0; i < cellsCount; i++) {
            _vCellsPositions[i] = currentPos;
            cellSize = _dataSource->tableCellSizeForIndex(this, i);
            switch (this->getDirection()) {
            case Direction::HORIZONTAL:
                currentPos += cellSize.width;
                break;
            default:
                currentPos += cellSize.height;
                break;
            }
        }
        _vCellsPositions[cellsCount] = currentPos; // 1 extra value allows us to
        // get right/bottom of the last
        // cell
    }
}

void TableView::scrollViewDidScroll(ScrollView* view)
{
    long countOfItems = _dataSource->numberOfCellsInTableView(this);
    if (0 == countOfItems) {
        return;
    }

    if (_isUsedCellsDirty) {
        _isUsedCellsDirty = false;
        std::sort(_cellsUsed.begin(), _cellsUsed.end(),
            [](TableViewCell* a, TableViewCell* b) -> bool {
                return a->getIdx() < b->getIdx();
            });
    }

    if (_tableViewDelegate != nullptr) {
        _tableViewDelegate->scrollViewDidScroll(this);
    }

    ssize_t startIdx = 0, endIdx = 0, idx = 0, maxIdx = 0;
    Vec2 offset = this->getContentOffset() * -1;
    maxIdx = MAX(countOfItems - 1, 0);

    if (_vordering == VerticalFillOrder::TOP_DOWN) {
        offset.y = offset.y + _viewSize.height / this->getContainer()->getScaleY();
    }
    startIdx = this->_indexFromOffset(offset);
    if (startIdx == CC_INVALID_INDEX) {
        startIdx = countOfItems - 1;
    }

    if (_vordering == VerticalFillOrder::TOP_DOWN) {
        offset.y -= _viewSize.height / this->getContainer()->getScaleY();
    }
    else {
        offset.y += _viewSize.height / this->getContainer()->getScaleY();
    }
    offset.x += _viewSize.width / this->getContainer()->getScaleX();

    endIdx = this->_indexFromOffset(offset);
    if (endIdx == CC_INVALID_INDEX) {
        endIdx = countOfItems - 1;
    }

#if 0 // For Testing.
    Ref* pObj;
    int i = 0;
    CCARRAY_FOREACH(_cellsUsed, pObj)
    {
        TableViewCell* pCell = static_cast<TableViewCell*>(pObj);
        log("cells Used index %d, value = %d", i, pCell->getIdx());
        i++;
    }
    log("---------------------------------------");
    i = 0;
    CCARRAY_FOREACH(_cellsFreed, pObj)
    {
        TableViewCell* pCell = static_cast<TableViewCell*>(pObj);
        log("cells freed index %d, value = %d", i, pCell->getIdx());
        i++;
    }
    log("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
#endif

    if (!_cellsUsed.empty()) {
        auto cell = _cellsUsed.at(0);
        idx = cell->getIdx();

        while (idx < startIdx) {
            this->_moveCellOutOfSight(cell);
            if (!_cellsUsed.empty()) {
                cell = _cellsUsed.at(0);
                idx = cell->getIdx();
            }
            else {
                break;
            }
        }
    }
    if (!_cellsUsed.empty()) {
        auto cell = _cellsUsed.back();
        idx = cell->getIdx();

        while (idx <= maxIdx && idx > endIdx) {
            this->_moveCellOutOfSight(cell);
            if (!_cellsUsed.empty()) {
                cell = _cellsUsed.back();
                idx = cell->getIdx();
            }
            else {
                break;
            }
        }
    }

    for (long i = startIdx; i <= endIdx; i++) {
        if (_indices->find(i) != _indices->end()) {
            continue;
        }
        this->updateCellAtIndex(i);
    }
}


bool TableView::onTouchBegan(Touch* pTouch, Event* pEvent)
{
    for (Node* c = this; c != nullptr; c = c->getParent()) {
        if (!c->isVisible()) {
            return false;
        }
    }

    _cell_move_direction = Direction::NONE;
    if (_del_cell_status && _touchedShowDelCell) {
        auto pos = _touchedShowDelCell->getPosition();
        _touchedShowDelCell->stopAllActions();
        _touchedShowDelCell->runAction(MoveTo::create(DEL_SIGN_MOVE_TIME, Vec2(0, pos.y)));
        _touchedShowDelCell = nullptr;
        return true;
    }

    bool touchResult = ScrollView::onTouchBegan(pTouch, pEvent);

    if (_touches.size() == 1) {
        long index;
        Vec2 point;

        point = this->getContainer()->convertTouchToNodeSpace(pTouch);

        index = this->_indexFromOffset(point);
        if (index == CC_INVALID_INDEX) {
            _touchedCell = nullptr;
        }
        else {
            _touchedCell = this->cellAtIndex(index);
        }

        if (_touchedCell && _tableViewDelegate != nullptr) {
            _tableViewDelegate->tableCellHighlight(this, _touchedCell);
        }
    }
    else if (_touchedCell) {
        if (_tableViewDelegate != nullptr) {
            _tableViewDelegate->tableCellUnhighlight(this, _touchedCell);
        }

        _touchedCell = nullptr;
    }

    _touch_began_point = pTouch->getLocation();
    return touchResult;
}

void TableView::onTouchMoved(Touch* pTouch, Event* pEvent)
{
    if (_del_cell_status && _touchedCell) {
        auto _touchMovePosition = pTouch->getLocation();
        if (_cell_move_direction == Direction::NONE) {
            auto x = std::abs(_touchPoint.x - _touchMovePosition.x);
            auto y = std::abs(_touchPoint.y - _touchMovePosition.y);
            if (x > y * 3) {
                _touch_move_point = _touchMovePosition;
                _cell_move_direction = Direction::HORIZONTAL;
                _touches.clear();
                return;
            }
            else if (y > x * 3)
                _cell_move_direction = Direction::VERTICAL;
        }

        if (_cell_move_direction == Direction::HORIZONTAL) {
            if (_show_del_state == DEL_CELL_MOVE_STATE::NONE) {
                if (_touch_move_point.x > _touchMovePosition.x) {
                    _show_del_state = DEL_CELL_MOVE_STATE::MOVE_LFET;
                }
            }
            else if (_show_del_state != DEL_CELL_MOVE_STATE::MOVE_OVER) {
                float offset = _touchMovePosition.x - _touch_move_point.x;
                if (offset > 0)
                    _show_del_state = DEL_CELL_MOVE_STATE::MOVE_RIGHT;
                else
                    _show_del_state = DEL_CELL_MOVE_STATE::MOVE_LFET;

                float posX = _touchedCell->getPosition().x + offset * 0.8;
                if (posX < -DEL_SIGN_WIDTH)
                    posX = -DEL_SIGN_WIDTH;
                else if (posX > 0)
                    posX = 0;
                _touchedCell->setPositionX(posX);
            }
            _touch_move_point = _touchMovePosition;
            _touches.clear();
            return;
        }
    }

    ScrollView::onTouchMoved(pTouch, pEvent);

    if (_touchedCell && isTouchMoved()) {
        if (_tableViewDelegate != nullptr) {
            _tableViewDelegate->tableCellUnhighlight(this, _touchedCell);
        }

        _touchedCell = nullptr;
    }
}

void TableView::onShowDelTouchOver(Touch* pTouch, Event* pEvent)
{
    auto pos = _touchedCell->getPosition();
    float offset = 0 - pos.x;
    float tagetPos = 0;
    _touchedCell->stopAllActions();
    if (offset >= DEL_SIGN_WIDTH)
        _touchedShowDelCell = _touchedCell;
    else if (offset >= DEL_SIGN_WIDTH / 2) {
        _touchedCell->runAction(
                                MoveTo::create(DEL_SIGN_MOVE_TIME, Vec2(-DEL_SIGN_WIDTH, pos.y)));
        _touchedShowDelCell = _touchedCell;
    }
    else if (offset < DEL_SIGN_WIDTH / 2) {
        _touchedCell->runAction(MoveTo::create(DEL_SIGN_MOVE_TIME, Vec2(0, pos.y)));
        _touchedShowDelCell = nullptr;
    }

}

void TableView::onTouchEnded(Touch* pTouch, Event* pEvent)
{
    if (!this->isVisible()) {
        return;
    }
    if (_del_cell_status && _touchedCell && _cell_move_direction == Direction::HORIZONTAL) {
        onShowDelTouchOver(pTouch, pEvent);
        return;
    }
    
    if (_touchedCell) {
        Rect bb = this->getBoundingBox();
        bb.origin = _parent->convertToWorldSpace(bb.origin);
        
        if (bb.containsPoint(pTouch->getLocation()) && _tableViewDelegate != nullptr) {
            if (!_bEatTouch) {
                _tableViewDelegate->tableCellUnhighlight(this, _touchedCell);
                _tableViewDelegate->tableCellTouched(this, _touchedCell);
            }
        }
        
        _touchedCell = nullptr;
    }
    _cell_move_direction = Direction::NONE;
    ScrollView::onTouchEnded(pTouch, pEvent);
}
void TableView::onTouchCancelled(Touch* pTouch, Event* pEvent)
{
    if (_del_cell_status && _touchedCell && _cell_move_direction == Direction::HORIZONTAL) {
        onShowDelTouchOver(pTouch, pEvent);
        return;
    }
    ScrollView::onTouchCancelled(pTouch, pEvent);

    if (_touchedCell) {
        if (_tableViewDelegate != nullptr) {
            _tableViewDelegate->tableCellUnhighlight(this, _touchedCell);
        }

        _touchedCell = nullptr;
    }
}

NS_CC_EXT_END
